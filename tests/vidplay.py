#!/usr/bin/env python3
"""VIDEO.O88 plays a .V88 fullscreen and silent, frame-exact and on time -
SPEC.md 98.3, VIDEO-PLAN wave 3.

    make && python3 tests/vidplay.py [--layout cga|herc] [--machine NAME]

THE CLIP IS MADE HERE, so the row needs no samples: 150 frames at 30 fps,
640x200, with PCM8 audio the silent player must step over. Moving shapes,
white fills and bursts of noise, so the stream crosses several 32 KB chunks
and holds records of every list (tools/os88vid.py's own fixture shapes). It
goes on a scratch 360KB floppy with VIDEO.O88 and is opened by
double-clicking it: the association is the launch path a user takes.

TWO PLAYS, two questions:

1. IS EVERY FRAME RIGHT? The ring is held to K = 2 slots (vp_kmax), so the
   stream wraps it several times and a super-packet has to run on into the
   mirror slot. The player HOLDS before chosen frames (vp_stopat), and at
   each hold the adapter's memory must equal tools/os88vid.py's reference
   decode of the frame before, byte for byte over the canvas.
2. IS IT ON TIME? With the default ring the clip is read whole before the
   first frame. So it must play all 150 frames with no stall and no late
   period, in 150/30 s: [ticks] within 2 of 82, and the guest's cycle
   counter within 10% of the same.

--layout herc makes a Hercules-layout clip and plays it on the Hercules 5150,
at its centred origin (SPEC.md 98.1.2).

--screen names a DIFFERENT layout's machine, and the clip then plays through
the SHADOW (SPEC.md 98.3.2): decoded into a RAM image of its own layout and
copied to the screen a band of rows at a time. Each hold compares the screen
at the rows the copy re-addressed them to, and waits for the copy (the band
empty) as well as the frame.

The holds include one straight after every frame whose video runs from slot
K-1 into the mirror, computed on the host, and the row FAILS if the clip has
none. Broken on purpose - vp_fill's mirror copy skipped - those holds FAIL.
"""
import argparse
import os
import random
import struct
import sys
import tempfile
import subprocess
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty, os88ui, os88build, os88vid as vid, os88geom as geom  # noqa: E402
from cycweb import pkg_syms                                   # noqa: E402

NF = 150
FPS = 30.0
MACHINE = {"cga": "os8088_5150_cga_gla", "herc": "os8088_5150_herc_gla",
           "lin80": "os8088_xt_vga"}
VSEG = {"cga": 0xB8000, "herc": 0xB0000, "lin80": 0xA0000}
VSIZE = {"cga": 16384, "herc": 65536, "lin80": 38400}
# MODE 12h IS PLANAR and a read of A000 is one plane, through the Read Map -
# but a MONO1 byte is written to ALL FOUR (Map Mask 0Fh, SPEC.md 98.1.2), so
# plane 0 IS the picture, and MartyPC's debug read returns it
ROWS = {"cga": 200, "herc": 348, "lin80": 480}
STOPS = (1, 17, 40, 63, 88, 111, 137, NF)


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


def clip(tmp, layout):
    """150 canvases 80 x 200, and the .V88 made of them."""
    rnd = random.Random(9898)
    wb, h = 80, 200
    cv = bytearray(wb * h)
    paths = []
    for f in range(NF):
        k = f % 25
        if k == 0:
            cv = bytearray(b"\xff" * (wb * h)) if f % 50 else bytearray(wb * h)
        elif k in (5, 6, 7, 8, 9):
            # noise: long slices, ~3 KB a frame. NOT 16 KB: that is ~67 ms of
            # decode against a 33 ms period, and the encoder that would budget
            # it is wave 8's - so such a clip is SUPPOSED to run late
            for _ in range(400):
                a = rnd.randrange(wb * h - 8)
                cv[a:a + 8] = bytes(rnd.getrandbits(8) for _ in range(8))
        elif k == 12:
            for y in range(40, 160):                # a band of one value
                cv[y * wb + 10:y * wb + 70] = b"\x55" * 60
        else:
            x, y = (f * 3) % (wb - 8), (f * 5) % (h - 24)
            for r in range(24):                     # a moving box
                cv[(y + r) * wb + x:(y + r) * wb + x + 8] = \
                    bytes([0x3C ^ (f & 0xFF)]) * 8
            for _ in range(30):                     # and isolated bytes
                cv[rnd.randrange(wb * h)] = rnd.getrandbits(8)
        p = os.path.join(tmp, "f%03d.pbm" % f)
        vid._write_pbm(p, wb, h, bytes(cv))
        paths.append(p)
    wav = os.path.join(tmp, "a.wav")
    vid._write_wav(wav, 8040, bytes(rnd.getrandbits(8)
                                    for _ in range(int(8040 * NF / FPS))))
    out = os.path.join(tmp, "CLIP.V88")
    vid.encode_frames(paths, out, FPS, wav, layout, "vidplay clip")
    vid.verify_v88(out)
    return out


def mirror_frames(r, clb, k=2):
    """The frames whose VIDEO part straddles slot K-1 into the mirror when the
    stream is read from the cluster boundary under its start (SPEC.md 98.3) -
    the only records a broken mirror copy can spoil. The audio at a record's
    end is skipped by a silent player, so a straddle that is only audio
    proves nothing and is not counted."""
    base = r.sp0 - r.sp0 % clb
    out, f = [], 0
    at, n = r.sp0, r.sp0n
    while n:
        sp = r.d[at:at + n * vid.SECTOR]
        nf, nxt = struct.unpack_from("<HH", sp, 0)
        o = 4
        for _ in range(nf):
            ln = struct.unpack_from("<H", sp, o)[0]
            s = at - base + o
            e = s + ln - r.abytes
            if s // 32768 != (e - 1) // 32768 and (s // 32768) % k == k - 1:
                out.append(f)
            o += ln
            f += 1
        at, n = at + n * vid.SECTOR, nxt
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--layout", choices=sorted(MACHINE), default="cga")
    ap.add_argument("--comp", action="store_true",
                    help="mark the clip CGACOMP: on a real CGA the play must "
                    "turn the colour burst on, and nowhere else (98.3.3)")
    ap.add_argument("--screen", choices=sorted(MACHINE),
                    help="play on this layout's machine: the shadow path")
    ap.add_argument("--machine")
    ap.add_argument("--stops", help="comma-separated holds (a diagnosis)")
    a = ap.parse_args()
    global STOPS
    if a.stops:
        STOPS = tuple(int(x) for x in a.stops.split(","))
    screen = a.screen or a.layout
    shadow = screen != a.layout
    machine = a.machine or MACHINE[screen]
    os.chdir(ROOT)
    syms, image = pkg_syms("apps/video/video.asm", ("apps/",))
    pkg = os88build.at("build/video.o88")
    if not os.path.exists(pkg):
        sys.exit("vidplay: no build/video.o88 - run `make`")
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        v88 = clip(tmp, a.layout)
        if a.comp:                  # CGACOMP: the same bytes, read as
            with open(v88, "r+b") as f:     # composite colours (98.1.1)
                f.seek(192)
                f.write(bytes([2]))
        r = vid.Reader(v88)
        g = r.g
        size = os.path.getsize(v88)
        disk = os.path.join(tmp, "vidplay.img")
        subprocess.run([sys.executable, "tools/os88disk.py", "-o", disk,
                        "--size", "360", pkg, v88], check=True,
                       capture_output=True)
        # the origin the player centres to (SPEC.md 98.1.2)
        # where each canvas row lands on the SCREEN: the file's own layout at
        # its centred origin, or the shadow's target layout, re-addressed
        tl = vid.LAYOUT_BY_NAME[screen]
        tg = vid.Geom(tl, g.wb, ROWS[screen])
        ty0 = (ROWS[screen] - g.h) // 2 // tg.banks * tg.banks
        tx0 = (tg.stride - g.wb) // 2
        rows_at = [tg.base[y + ty0] + tx0 for y in range(g.h)]
        bad = []
        with os88ui.boot(os88build.at("build/os8088-360.img"), apps=disk,
                         machine=machine) as ui:
            m = ui.m
            w = ui.path("B:/CLIP.V88")
            rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE,
                         geom.WIN_SIZE)
            base = u16(rec, geom.W_SEG) << 4

            def rw(n):
                return u16(m.read(base + syms[n], 2))

            def rb(n):
                return m.read(base + syms[n], 1)[0]

            def ww(n, v):
                m.write(base + syms[n], struct.pack("<H", v))

            os88marty.until(m, lambda mm: rb("vp_loaded") == 1,
                            "the clip's header to be read", poll=0.5,
                            limit=300.0, guest=30.0)
            if rb("vp_ok") != 1:
                sys.exit("vidplay: the player will not play the clip here")
            m.write(base + syms["vp_nowin"], b"\1")   # FULL SCREEN: the
                                            # window's play is tests/vidwin.py
            if rb("vp_shadow") != int(shadow):
                bad.append("the player chose %s where the %s screen wants %s"
                           % ("the shadow" if rb("vp_shadow") else "native",
                              screen, "the shadow" if shadow else "native"))
            # a hold straight after every frame that runs into the mirror, or
            # the row would pass with the mirror copy deleted
            mf = mirror_frames(r, rw("vp_clsec") * 512)
            stops = tuple(sorted(set(STOPS) | {f + 1 for f in mf}))
            print("   frames whose video runs into the mirror slot: %s" % mf)
            if not mf and not a.stops and a.layout in VSEG:
                bad.append("the clip never runs a frame into the mirror, so "
                           "the ring's wrap is untested")
            # --- 1: every frame right, the ring held to two slots
            ww("vp_kmax", 2)
            ww("vp_stopat", stops[0])
            m.write(base + syms["vp_played"], b"\0")
            m.type_text("p")
            def state():
                return " ".join("%s=%d" % (k, rw(k)) for k in (
                    "vp_done", "vp_lc", "vp_pc", "vp_po", "vp_psec",
                    "vp_nsec", "vp_fleft", "vp_rofs", "vp_stall", "vp_k",
                    "vp_owed")) + " end=%d err=%d eof=%d held=%d ready=%d" % (
                    rb("vp_end"), rb("vp_err"), rb("vp_eof"), rb("vp_held"),
                    rb("vp_ready"))

            def until(cond, what, guest):
                try:
                    os88marty.until(m, cond, what, poll=0.3, limit=600.0,
                                    guest=guest)
                except os88marty.MartyError:
                    print("   TIMED OUT: %s\n     state: %s" % (what, state()))
                    raise

            for n in stops:
                # THE HOLD IS vp_done REACHING n, not the flag alone: the hook
                # sets vp_held on every period it spends holding, so a flag
                # read straight after the stop moved can be the OLD stop's
                until(lambda mm: rb("vp_held") == 1 and rw("vp_done") == n
                      and (not shadow or rw("vp_dy1") == 0),
                      "the hold before frame %d" % n, 120.0)
                if screen not in VSEG:
                    print("   hold before frame %3d: (not read back)" % n)
                else:
                    seg = bytes(m.read(VSEG[screen], VSIZE[screen]))
                    got = b"".join(seg[b:b + g.wb] for b in rows_at)
                    want = vid.decode_at(r, n - 1)
                    diff = sum(1 for i in range(len(got)) if got[i] != want[i])
                    print("   hold before frame %3d: %d bytes of %d differ"
                          % (n, diff, len(got)))
                    if diff:
                        bad.append("the screen before frame %d differs in %d "
                                   "bytes" % (n, diff))
                        print("     state: " + state())
                i = stops.index(n)
                ww("vp_stopat", stops[i + 1] if i + 1 < len(stops) else 0xFFFF)
                m.write(base + syms["vp_held"], b"\0")
            until(lambda mm: rb("vp_played") == 1, "the first play to end",
                  60.0)
            done1, err1 = rw("vp_done"), rb("vp_err")
            k1 = rw("vp_k")
            # --- 2: on time, the ring big enough for the whole clip
            ww("vp_kmax", 8)
            m.write(base + syms["vp_played"], b"\0")
            m.type_text("p")
            os88marty.until(m, lambda mm: rb("vp_ready") == 1,
                            "the second play to start", poll=0.1,
                            limit=300.0, guest=60.0)
            c0 = int(m.status().get("cycles", 0))
            os88marty.until(m, lambda mm: rb("vp_ready") == 0,
                            "the second play to end", poll=0.1, limit=300.0,
                            guest=60.0)
            c1 = int(m.status().get("cycles", 0))
            os88marty.until(m, lambda mm: rb("vp_played") == 1,
                            "the bracket to return", poll=0.5, limit=120.0,
                            guest=30.0)
            done2, err2 = rw("vp_done"), rb("vp_err")
            stall, late, dt, k2 = (rw("vp_stall"), rw("vp_late"), rw("vp_dt"),
                                   rw("vp_k"))
            gap = rw("vp_gap")
            burst = rb("vp_burst")
    want_t = NF / FPS * 1193182 / 65536
    secs = (c1 - c0) / 4772727.0
    print("\n   %s clip, %d frames, %d bytes; first play K=%d, drew %d"
          % (a.layout, NF, size, k1, done1))
    print("   second play K=%d: drew %d, stalls %d, late %d, %d ticks "
          "(want %.1f); the guest's clock %.2f s (want %.2f)"
          % (k2, done2, stall, late, dt, want_t, secs, NF / FPS))
    if k1 != 2:
        bad.append("the first play's ring was %d slots, not 2" % k1)
    if done1 != NF or err1:
        bad.append("the first play drew %d of %d (error %d)"
                   % (done1, NF, err1))
    if done2 != NF or err2:
        bad.append("the second play drew %d of %d (error %d)"
                   % (done2, NF, err2))
    if stall or late:
        bad.append("a clip read whole first stalled %d and was late %d"
                   % (stall, late))
    # the SHADOW's copy holds the hook off for a full-screen band (~6 periods
    # on Hercules), and the decode catches up behind it: the display rate is
    # what drops, and the end can trail by the backlog of the last heavy
    # stretch - so its bound is wider than a native play's
    if a.comp:                      # 98.3.3: a real CGA's mode 6 only
        want_b = int(screen == "cga" and "vga" not in machine)
        print("   CGACOMP: the colour burst %s (want %s)"
              % ("ON" if burst else "off", "ON" if want_b else "off"))
        if burst != want_b:
            bad.append("the colour burst was %s" % ("on" if burst else "off"))
    tol = 4 if shadow else 2
    print("   the hook was held off at most %d periods" % gap)
    if abs(dt - want_t) > tol:
        bad.append("%d ticks for %.1f s of video" % (dt, NF / FPS))
    if abs(secs - NF / FPS) > 0.1 * NF / FPS:
        bad.append("the guest's clock says %.2f s" % secs)
    for b in bad:
        print("   FAIL: %s" % b)
    if not bad:
        print("   ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
