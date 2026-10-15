#!/usr/bin/env python3
"""VIDEO.O88 plays a file's LAYER (SPEC.md 98.1.9) - on MartyPC's 5150 with
a Hercules, off a 360 KB floppy.

    make && python3 tests/vidlyplay.py

THE CLIP IS MADE HERE: ffmpeg's Life, four seconds at 15 fps, encoded by
tools/os88venc.py for a 12 KB/s disk with a layer for 40 KB/s banked in 128
KB - small enough that the layer's FOUR slots hold all of it, so the player
reads it whole before the first frame and every layer record is there when
its frame is drawn. Nothing is timed; every wait is on guest state.

  1. Play, held before a handful of frames (vp_stopat): the Hercules screen
     must be tools/os88vid.py's decode of BASE THEN LAYER to the frame
     before, byte for byte - and the player must have drawn layer records
     and left none out (vp_lygot > 0, vp_lyskp and vp_lymis 0);
  2. the play reaches the end;
  3. THE NEGATIVE CONTROL: the layer's header word zeroed in the player
     (vp_lysp), Play again: the screen must be the BASE's decode at the
     same holds and must NOT be the layered one - so the check sees a
     layer, and a player that reads none plays the base exactly;
  4. A SEEK: the layer's word put back, Right picks key 1 and Play starts
     there - the layer's cursor at the record its KEY TABLE names, read
     from the 32 KB chunk that holds it (98.1.9.8) - and at holds past the
     key the screen must be the key's picture then base and layer from its
     next frame.

THE DISK TEST (98.1.9.3) would refuse every arm above, and rightly: the
layer is for a 40 KB/s disk and this is a 12 KB/s floppy. These arms test
the DECODE, so their clip's two disk words (H_LYBKS, H_LYIDL) are zeroed and the CPU
decides alone - and from frame 0 it has no key to time, which is NO VERDICT
and plays the base (98.1.9.7). So the plain arm first plays with nothing
poked and must be the base's with the card's 'Layer off: not timed' (red
with 98.1.9.7 taken out: it plays the layer); then key 0's verdict is poked
in as kept and passed ([vp_lycv] 1), which is what a machine that timed key
0 carries into a play from frame 0, and the arms above run on it. The
key-1 arms clear it again, so the bench decides them. --disk keeps it, plays once and asserts the refusal: the
layer off for the DISK, the screen the base's at every hold, and the card's
line 4 naming the rate the ring's fill was timed at - between 9 and 18 KB/s
for a 360 KB floppy on a cycle-exact 5150, which is the instrument's own
check (QEMU's floppy stalls the guest and cannot be timed, 98.1.9.3).

--flip runs any arm with the layer's OWN claim refused (vp_lynoc, a gate's
byte): its slots are then the ring claim's top (98.1.9.4, THE LIVE FLIP),
and every hold must still be exact - the layered decode where the layer is
read out of the carved slots, and with --disk the BASE's where the disk's
"no" after three chunks gave the slots back to the ring and copied its
mirror up to the ring's whole end. Each play's first hold says the flip
was taken: [vp_lyfl] set, and either [vp_lyb] the slots lent (read) or
[vp_k] the ring's own again (given back).

--noroom is the flip REFUSED: the layer's claim refused and the ring capped
at five slots ([vp_kmax]), so lending two would leave it under four - the
base plays alone, exact at every hold, and the card's line 4 says why:
'Layer off: no room, N KB short' (98.1.9.4).

--stream is the other shape: six seconds banked in 64 KB, so the layer
is many times its two slots and the reader must refill them during the
play, at the ring-full moment, off a floppy that cannot quite keep up. The
play must reach the end with no error, the slots must have been read over
again, records must have been drawn - and at every hold where none had been
left out yet and the layer had caught up to it, the screen is still exact.
The records run on across the 32 KB chunks, and one runs out of the LAST
slot into the next chunk through the mirror (98.1.9.8): broken on purpose
(vp_lymir a bare `ret`) the hold before frame 89 is 16 bytes off.
"""
import os
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path[:0] = [HERE, os.path.join(ROOT, "tools")]
import os88build, os88marty, os88ui, os88vid as vid           # noqa: E402
import os88geom as geom                                       # noqa: E402
from cycweb import pkg_syms                                   # noqa: E402

IMG = "build/os8088-360.img"
MACHINE = "os8088_5150_herc_gla"
STOPS = tuple(int(x) for x in os.environ.get("VIDLY_STOPS", "12,30,59").split(","))
SKIP = 77


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


def clip(tmp, secs, mem, bks=False):
    src = os.path.join(tmp, "life.mkv")
    subprocess.run(
        ["ffmpeg", "-v", "error", "-f", "lavfi", "-i",
         "life=s=400x200:mold=10:r=15:ratio=0.4:death_color=#000000:"
         "life_color=#ffffff,trim=duration=%d" % secs, "-c:v", "ffv1", src],
        check=True)
    out = os.path.join(tmp, "LAY.V88")
    subprocess.run(
        [sys.executable, "tools/os88venc.py", src, out, "--quiet",
         "--preset", "herc", "--profile", "5150-st225", "--audio", "none",
         "--fps", "15", "--disk", "12000", "--reserve", "32",
         "--layer-disk", "40000", "--layer-memory", str(mem)], check=True)
    if not bks:                     # (the decode under test, not the disk)
        d = bytearray(open(out, "rb").read())
        struct.pack_into("<HH", d, vid.H_LYBKS, 0, 0)   # (and H_LYIDL)
        open(out, "wb").write(d)
    vid.verify_v88(out)
    return out


def from_key(r, i, stops, layer=True):
    """{n: the canvas before frame n} for a play from key i: its picture,
    then base and layer from its next frame on"""
    k, krec, spo, spk, idx = r.key(i)
    surf = r.g.surface()
    r.apply(surf, krec, key=True)
    lit = r.layer_records(at=r.layer_key(i), frame0=k + 1)
    out = {}
    f = k + 1
    for rec, at, j in r.records(at=spo, nsec=spk, skip=idx):
        r.apply(surf, rec, check=False)
        for lf, lrec, *_ in lit:
            if lf == f:
                if lrec is not None and layer:
                    r.apply(surf, lrec, check=False, layer=True)
                break
        if f + 1 in stops:
            out[f + 1] = r.g.canvas(surf)
        f += 1
    return out


def main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--stream", action="store_true")
    ap.add_argument("--disk", action="store_true")
    ap.add_argument("--flip", action="store_true")
    ap.add_argument("--noroom", action="store_true")
    a = ap.parse_args()
    global STOPS
    if a.stream:
        STOPS = (30, 60, 89)
    if not shutil.which("ffmpeg"):
        print("   SKIP: needs ffmpeg")
        return SKIP
    os.chdir(ROOT)
    for p in (IMG, "build/video.o88"):
        if not os.path.exists(os88build.at(p)):
            sys.exit("vidlyplay: no %s - run `make`" % p)
    syms, image = pkg_syms("apps/video/video.asm", ("apps/",))
    bad = []
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        v88 = clip(tmp, 6 if a.stream else 4, 64 if a.stream else 128,
                   bks=a.disk)
        r = vid.Reader(v88)
        lrec = sum(1 for f, rec, *_ in r.layer_records() if rec is not None)
        lslots = -(-(len(r.d) - r.lsp0) // vid.SLOT)
        print("   clip: %d frames %d x %d, layer of %d records in %d slots"
              % (r.frames, r.g.wb * 8, r.g.h, lrec, lslots))
        layered = {}
        for f, surf in vid.v88_layered(r):
            if f + 1 in STOPS:
                layered[f + 1] = r.g.canvas(surf)
        plain = {}
        for f, surf, rec, at, i in vid.v88_frames(r):
            if f + 1 in STOPS:
                plain[f + 1] = r.g.canvas(surf)
        disk = os.path.join(tmp, "lay.img")
        subprocess.run([sys.executable, "tools/os88disk.py", "-o", disk,
                        "--size", "360", os88build.at("build/video.o88"),
                        v88], check=True, capture_output=True)
        WB, H = r.g.wb, r.g.h
        tg = vid.Geom(vid.LAY_HERC, WB, 348)
        ty0 = (348 - H) // 2 // tg.banks * tg.banks
        tx0 = (tg.stride - WB) // 2
        rows_at = [tg.base[y + ty0] + tx0 for y in range(H)]
        with os88ui.boot(os88build.at(IMG), apps=disk, machine=MACHINE) as ui:
            m = ui.m
            w = ui.path("B:/LAY.V88")
            rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE,
                         geom.WIN_SIZE)
            base = u16(rec, geom.W_SEG) << 4

            def rw(n):
                return u16(m.read(base + syms[n], 2))

            def rb(n):
                return m.read(base + syms[n], 1)[0]

            def ww(n, v):
                m.write(base + syms[n], struct.pack("<H", v))

            if a.flip or a.noroom:      # (the layer's own claim refused)
                m.write(base + syms["vp_lynoc"], b"\1")
            if a.noroom:                # (...and a ring too small to lend)
                ww("vp_kmax", 5)
            flips = []

            def until(cond, what, guest=120.0):
                os88marty.until(m, lambda mm: cond(), what, poll=0.2,
                                limit=900.0, guest=guest)

            def state():
                return ("bench %d/%d/%d off=%d " % (rw("vp_lytm"),
                                                     rw("vp_lytl"),
                                                     rw("vp_lytb"),
                                                     rb("vp_lyoff")) +
                        "done=%d err=%d on=%d got=%d skp=%d mis=%d rd=%d "
                        "use=%d ns=%d seg=%04x eof=%d sp=%08x kb=%d clb=%d k=%d" % (
                            rw("vp_done"), rb("vp_err"), rb("vp_lyon"),
                            rw("vp_lygot"), rw("vp_lyskp"), rw("vp_lymis"),
                            rw("vp_lyrd"), rw("vp_lyuse"), rw("vp_lyns"),
                            rw("vp_lyseg"), rb("vp_lyeof"),
                            struct.unpack("<I", m.read(base + syms["vp_lysp"],
                                                       4))[0],
                            rw("vp_lykb"), rw("vp_clb"), rw("vp_k")))

            def play(want, label, stops=STOPS):
                m.write(base + syms["vp_nowin"], b"\1")
                ww("vp_stopat", stops[0])
                m.write(base + syms["vp_played"], b"\0")
                m.type_text("p")
                for i, n in enumerate(stops):
                    until(lambda: rb("vp_held") == 1 and rw("vp_done") == n,
                          "the hold before frame %d" % n)
                    if i == 0:
                        flips.append((label, rb("vp_lyfl"), rb("vp_lyb"),
                                      rw("vp_k"), rw("vp_lyk0"),
                                      rb("vp_lyon")))
                        print("   %s: flip %d, lent %d, ring %d (of %d), "
                              "layer %s" % ((label,) + flips[-1][1:5] +
                                            ("on" if flips[-1][5]
                                             else "off",)))
                    seg = bytes(m.read(0xB0000, 65536))
                    got = b"".join(seg[b:b + WB] for b in rows_at)
                    dl = sum(1 for j in range(len(got))
                             if got[j] != layered[n][j])
                    dp = sum(1 for j in range(len(got))
                             if got[j] != plain[n][j])
                    print("   %s, hold before frame %2d: %4d bytes differ "
                          "from the layered decode, %4d from the base's "
                          "(%s)" % (label, n, dl, dp, state()))
                    # exact only where nothing was left out AND the layer
                    # has caught up to the hold: a slot not read yet holds
                    # records the hook cannot count, and counts when it
                    # arrives (vp_lydec steps over them then)
                    exact = not (rw("vp_lyskp") or rw("vp_lymis")) and \
                        (rw("vp_lynx") >= n or not rb("vp_lyon"))
                    if (dl if want == "layered" else dp) and \
                            (want != "layered" or exact):
                        bad.append("%s: the screen before frame %d is not "
                                   "the %s decode" % (label, n, want))
                    if want == "base" and not dl and dp:
                        bad.append("%s: the base-only play drew the layer"
                                   % label)
                    ww("vp_stopat", stops[i + 1] if i + 1 < len(stops)
                       else 0xFFFF)
                    m.write(base + syms["vp_held"], b"\0")
                until(lambda: rb("vp_played") == 1 and rb("vp_ready") == 0,
                      "the play to end")
                print("   %s: the end: %s" % (label, state()))
                if rw("vp_done") != r.frames or rb("vp_err"):
                    bad.append("%s drew %d of %d (error %d)"
                               % (label, rw("vp_done"), r.frames,
                                  rb("vp_err")))

            def line4():
                return m.read(base + syms["vp_lines"] + 4 * 36, 35).split(
                    b"\0")[0].decode("ascii", "replace").rstrip()
            if a.noroom:                # THE FLIP REFUSED (98.1.9.4), alone
                play("base", "no room")
                if rb("vp_lyfl") != 1 or rw("vp_lygot"):
                    bad.append("no room: the claim refused %d, %d layer "
                               "records drawn" % (rb("vp_lyfl"),
                                                  rw("vp_lygot")))
                try:
                    until(lambda: line4().startswith("Layer off: no room, "),
                          "the card's room line", 20.0)
                except Exception:
                    pass
                print("   the card's line 4: %r" % line4())
                if not (line4().startswith("Layer off: no room, ") and
                        line4().endswith(" KB short")):
                    bad.append("the card's line 4 is %r, not 'Layer off: no "
                               "room, N KB short'" % line4())
            elif a.disk:                # THE DISK TEST (98.1.9.3), alone
                play("base", "the disk saying no")
                dkb = rw("vp_lydkb")
                print("   the disk test: verdict %d, this disk %d KB/s, the "
                      "file's %d and %d" % (rb("vp_lydv"), dkb,
                                            rw("vp_lykbs"), rw("vp_lybks")))
                if rb("vp_lydv") != 2 or not rb("vp_lywhy") or \
                        rw("vp_lygot"):
                    bad.append("the disk test did not refuse the layer "
                               "(verdict %d, reason %d, %d drawn)"
                               % (rb("vp_lydv"), rb("vp_lywhy"),
                                  rw("vp_lygot")))
                if not 9 <= dkb <= 18:
                    bad.append("the 360 KB floppy timed at %d KB/s, not 9 "
                               "to 18" % dkb)
                want4 = "Layer off: disk %d KB/s, its %d" % (
                    dkb, rw("vp_lykbs"))
                try:
                    until(lambda: line4() == want4, "the card's disk line",
                          20.0)
                except Exception:
                    pass
                print("   the card's line 4: %r" % line4())
                if line4() != want4:
                    bad.append("the card's line 4 is %r, not %r"
                               % (line4(), want4))
            else:
                if not a.stream and not a.flip:
                    # NO VERDICT IS THE BASE (98.1.9.7): no disk words, no
                    # key from frame 0 - nothing has said yes
                    play("base", "no verdict")
                    try:
                        until(lambda: line4() == "Layer off: not timed",
                              "the card's no-verdict line", 20.0)
                    except Exception:
                        pass
                    print("   the card's line 4: %r" % line4())
                    if rw("vp_lygot") or line4() != "Layer off: not timed":
                        bad.append("no verdict: %d layer records drawn, the "
                                   "card's line 4 %r, not 'Layer off: not "
                                   "timed'" % (rw("vp_lygot"), line4()))
                # (after the open's vp_xopen, which clears it: vp_loaded
                # goes to 1 just before it, so wait a moment past that)
                until(lambda: rb("vp_loaded") == 1, "the clip's header")
                os88marty.pace(m, 0.5)
                m.write(base + syms["vp_lycv"], b"\1")    # (key 0: passed)
                play("layered", "the layer")
                if rw("vp_lygot") == 0:
                    bad.append("the player drew no layer record")
                # THE CARD SAYS SO (line 4 after a play that read a layer)
                want = "Layer drew %d, %d missed, %d late" % (
                    rw("vp_lygot"), rw("vp_lymis"), rw("vp_lyskp"))
                try:
                    until(lambda: line4() == want, "the card's layer line", 20.0)
                except Exception:
                    pass
                print("   the card's line 4: %r" % line4())
                if line4() != want:
                    bad.append("the card's line 4 is %r, not %r" % (line4(), want))
                if a.stream:
                    if rw("vp_lyrd") <= rw("vp_lyns"):
                        bad.append("the layer's %d slots were read %d times: "
                                   "the reader never refilled them"
                                   % (rw("vp_lyns"), rw("vp_lyrd")))
                    print("   streamed: %d records drawn, %d left out behind, "
                          "%d read too late, %d slot reads"
                          % (rw("vp_lygot"), rw("vp_lyskp"), rw("vp_lymis"),
                             rw("vp_lyrd")))
                elif rw("vp_lyskp") or rw("vp_lymis"):
                    bad.append("the player left %d layer records out behind and "
                               "%d read too late" % (rw("vp_lyskp"),
                                                     rw("vp_lymis")))
                if not a.stream:
                    sp = m.read(base + syms["vp_lysp"], 4)
                    m.write(base + syms["vp_lysp"], bytes(4))
                    play("base", "no layer")
                    # --- 4: from key 1, the layer's place out of its key table
                    m.write(base + syms["vp_lysp"], sp)
                    m.write(base + syms["vp_lycv"], b"\0")    # (the bench's)
                    n0 = rw("vp_ploads")
                    m.key("ArrowRight")
                    until(lambda: rw("vp_ploads") > n0, "Right to pick key 1")
                    k1 = r.keys[1][0]
                    ks = tuple(x for x in (k1 + 8, k1 + 20, r.frames - 1)
                               if x > k1 + 1)
                    layered.update(from_key(r, 1, ks))
                    plain.update(from_key(r, 1, ks, layer=False))
                    print("   key 1 is frame %d: holds at %s" % (k1, ks))
                    play("layered", "from key 1", ks)
                    if rw("vp_lygot") == 0:
                        bad.append("from key 1 the player drew no layer record")
                    print("   the bench: key 1 in %.2f ms, its model %.2f"
                          % (rw("vp_lytm") / 100.0, rw("vp_lytl") / 100.0))
                    if rb("vp_lyoff") or not rw("vp_lytm"):
                        bad.append("the bench did not pass this machine at its "
                                   "own model (%d against %d)"
                                   % (rw("vp_lytm"), rw("vp_lytl")))
                    # --- 5: THE BENCH SAYS NO (98.1.9): the layer's machine
                    # poked impossibly fast, so this CPU is the base's - the
                    # layer is not read, the screen is the BASE's, the card says
                    ytl = m.read(base + syms["vp_lytl"], 4)
                    ww("vp_lytl", 1)
                    ww("vp_lytb", 1)
                    n0 = rw("vp_ploads")
                    m.key("ArrowRight")
                    until(lambda: rw("vp_ploads") > n0, "Right to pick key 1 again")
                    if rw("vp_sel") != 1:
                        m.key("ArrowLeft")
                        until(lambda: rw("vp_sel") == 1, "back to key 1")
                    play("base", "the bench saying no", ks)
                    if not rb("vp_lyoff") or rw("vp_lygot"):
                        bad.append("the bench said yes to a machine past its line "
                                   "(off %d, %d drawn)" % (rb("vp_lyoff"),
                                                           rw("vp_lygot")))
                    want4 = "Layer off: key %d ms, its 0" % (rw("vp_lytm") // 100)
                    try:
                        until(lambda: line4() == want4, "the card's bench line",
                              20.0)
                    except Exception:
                        pass
                    print("   the card's line 4: %r" % line4())
                    if line4() != want4:
                        bad.append("the card's line 4 is %r, not %r"
                                   % (line4(), want4))
                    m.write(base + syms["vp_lytl"], ytl)
    if a.flip:
        for label, fl, lent, k, k0, on in flips:
            if label == "no layer":
                continue
            if not fl:
                bad.append("%s: the layer's claim was refused and no flip "
                           "was taken" % label)
            elif on and not lent:
                bad.append("%s: the layer read with no slots lent" % label)
            elif not on and (lent or k != k0):
                bad.append("%s: the layer off and the ring %d of its %d, %d "
                           "slots still lent" % (label, k, k0, lent))
        if not flips or not any(f[5] for f in flips) and not a.disk:
            bad.append("no play read the layer out of the lent slots")
    if bad:
        print("\nvidlyplay: FAIL")
        for x in bad:
            print("  - " + x)
        return 1
    print("\nvidlyplay: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
