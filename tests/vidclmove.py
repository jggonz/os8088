#!/usr/bin/env python3
"""THE VIDEO PLAYER'S SESSION CLAIMS MOVE BETWEEN BRACKETS - SPEC.md 98.3.19.2,
docs/plans/NO-PINNING-PLAN.md (A6's remainder).

    make && python3 tests/vidclmove.py --arm resident|layer|diag
        (make emstest for layer, make build/videodiag.o88 for diag;
         VIDCLMOVE_MAP=1 draws the heap, VIDCLMOVE_SHOT=x.png the glass of
         a failure)

A session left PAUSED on the desktop (Space, or a click, in the window -
98.3.7's DESK) holds its claims while nothing plays. `vp_smov` declares them
movable for that time and pins them again for the next bracket, whose hook
reads them at interrupt time. Three claims were left out of it, and one was
never even freed; each arm is one of them:

  resident  a RESIDENT file played from a KEY: the key's record is a claim of
            its own (vp_sstart's resident arm). It was stored in [vp_ring]
            alone, and vp_sfree gives back [vp_rclm] - so every such play
            LEAKED it. On the Hercules 5150.
  layer     THE LAYER's own claim ([vp_lyseg], 98.1.9), which a play IN PLACE
            makes after its ring (vp_lyown): on MartyPC's 8088 with the
            Lo-tech EMS board, booted from `make emstest`'s disk, the bank
            in the board's pages and the layer's slots in conventional
            memory. [vp_lycv] is poked to a kept yes so the 8088 reads the
            layer (tests/vidlyplay.py's reason) and every hold is the
            layered decode.
  diag      VIDEOD's PROFILE ([vp_prseg], 98.3.22): build/videodiag.o88 on
            the Hercules 5150, a streamed clip. The profile is claimed first,
            under the ring, and every frame's two times are written into it
            - so after the move the VPPROFn.DAT it saves must hold every
            frame, the ones drawn after it included.

EVERY ARM: a hole-maker A (a stored resident file, played once, its block
low in the heap) is opened first; the clip B plays IN ITS WINDOW to a hold,
and a click takes it back to the desktop paused, the session waiting. A
closes, which leaves a hole under B's claims, and a compaction is posted
from OUTSIDE at the trivial purge rank (`[mem_cpq_lvl]` 0xFC, `[mem_cpq]` =
B's window - tests/pkgmove.py's door). Then:

  1. every claim the arm is about is DECLARED (MC_RLOC = vp_smove);
  2. it MOVED (else the run proves nothing, and says so);
  3. every word names it afterwards ([vp_ring] inside [vp_rclm]'s claim, a
     derived word);
  4. its bytes are the same, hashed at both addresses;
  5. PLAY ON: Play resumes the session in the window, and at a hold after
     the move the window is the host's decode byte for byte; the play runs
     to its last frame with no error.
  resident, 6: after that end the session's claims are all given back - the
     key's record with them (the leak).
  diag, 6: B's VPPROFn.DAT (A, played first, wrote one too), read off B:,
     has 'VPPR', the play's frame count and a time in every frame's slot.

VERIFIED RED: vp_smove a bare `ret` - 2 and 3 FAIL in every arm (the word
names the old base; the layer arm's hold after it is 2,716 bytes off and
the diag arm's play never resumes); vp_smov's layer line taken out - 1
FAILS (layer); the resident arm's `mov [vp_rclm], dx` taken out - 1 and 6
FAIL, the key's record STILL HELD after the session, which is the leak.
"""
import argparse
import hashlib
import os
import random
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path[:0] = [HERE, os.path.join(ROOT, "tools")]
import os88build, os88marty, os88ui, os88vid as vid, os88geom as geom  # noqa
import os88fat                                                # noqa: E402
import heapmap                                                # noqa: E402
from cycweb import pkg_syms                                   # noqa: E402

SKIP = 77
HERC = "os8088_5150_herc_gla"
EMS = "os8088_5150_herc_hdd_sb_ems_gla"
WB, H = 80, 200


class Stop(Exception):
    pass


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


def noise(path, kb, seed):
    """the hole-maker: a stored resident Hercules file, its block ~kb KB
    (tests/vidmove.py's)"""
    rnd = random.Random(seed)
    g = vid.Geom(vid.LAY_HERC, 40, 100)
    w = vid.Writer(g, 1500, 100, vid.AUD_NONE, 0, vid.PF_MONO1, title="a",
                   keysecs=100.0)
    surf = g.surface()
    for f in range(max(2, kb * 1024 // 4300)):
        cv = rnd.randbytes(4000)
        for y, b in enumerate(g.base):
            surf[b:b + 40] = cv[y * 40:(y + 1) * 40]
        w.frame(vid.spans([b + x for b in g.base for x in range(40)],
                          surf, g), surf)
    vid.write_resident(path, [w], title="a", pack=vid.PK_LZB)


def canvases(nf):
    """noise that stays, so a KEY is a big record (the resident arm's claim
    lands over the low holes), a box that moves and a band, every frame
    different"""
    out = []
    cv = bytearray(random.Random(5).randbytes(WB * H))
    for f in range(nf):
        x, y = (f * 3) % (WB - 8), (f * 5) % (H - 24)
        for r in range(24):
            cv[(y + r) * WB + x:(y + r) * WB + x + 8] = \
                bytes([0x3C ^ (f & 0xFF)]) * 8
        cv[(f % H) * WB:(f % H) * WB + WB] = bytes([f & 0xFF]) * WB
        out.append(bytes(cv))
    return out


def clip_resident(tmp):
    out = os.path.join(tmp, "B.V88")
    g = vid.Geom(vid.LAY_HERC, WB, H)
    vid.encode_canvases(canvases(90), g, out, 15.0, title="b", keysecs=1.0,
                        resident=True)
    vid.verify_v88(out)
    return out


def clip_stream(tmp):
    out = os.path.join(tmp, "B.V88")
    g = vid.Geom(vid.LAY_HERC, WB, H)
    vid.encode_canvases(canvases(90), g, out, 15.0, title="b", keysecs=2.0)
    vid.verify_v88(out)
    return out


def clip_layer(tmp):
    """tests/vidlyplay.py's: ffmpeg's Life for a 12 KB/s disk with a layer
    for 40 KB/s, its disk words zeroed so the CPU decides"""
    src = os.path.join(tmp, "life.mkv")
    subprocess.run(
        ["ffmpeg", "-v", "error", "-f", "lavfi", "-i",
         "life=s=400x200:mold=10:r=15:ratio=0.4:seed=88:"
         "death_color=#000000:life_color=#ffffff,trim=duration=4",
         "-c:v", "ffv1", src], check=True)
    out = os.path.join(tmp, "B.V88")
    subprocess.run(
        [sys.executable, "tools/os88venc.py", src, out, "--quiet",
         "--preset", "herc", "--profile", "5150-st225", "--audio", "none",
         "--fps", "15", "--disk", "12000", "--reserve", "32",
         "--layer-disk", "40000", "--layer-memory", "128"], check=True)
    d = bytearray(open(out, "rb").read())
    struct.pack_into("<HH", d, vid.H_LYBKS, 0, 0)
    open(out, "wb").write(d)
    vid.verify_v88(out)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--arm", choices=("resident", "layer", "diag"),
                    default="resident")
    a = ap.parse_args()
    os.chdir(ROOT)
    diag = a.arm == "diag"
    pkgf = os88build.at("build/videodiag.o88" if diag else "build/video.o88")
    img = os88build.at("build/emstest.img" if a.arm == "layer"
                       else "build/os8088-360.img")
    for p in (pkgf, img):
        if not os.path.exists(p):
            sys.exit("vidclmove: no %s - run `make%s`" % (
                p, " emstest" if "emstest" in p else
                " build/videodiag.o88" if diag else ""))
    if a.arm == "layer" and not __import__("shutil").which("ffmpeg"):
        print("   SKIP: needs ffmpeg")
        return SKIP
    syms, _ = pkg_syms("apps/video/video.asm", ("apps/",),
                       ("VP_DIAG",) if diag else ())
    bad = []
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        tmp = os.path.abspath(tmp)
        av = os.path.join(tmp, "A.V88")
        noise(av, 48, 7)
        bv = {"resident": clip_resident, "layer": clip_layer,
              "diag": clip_stream}[a.arm](tmp)
        r = vid.Reader(bv)
        nf = r.frames
        if a.arm == "layer":
            want = {}
            for f, surf in vid.v88_layered(r):
                want[f] = r.g.canvas(surf)
            frame = want.__getitem__
            plain = {f: r.g.canvas(surf) for f, surf, *_ in vid.v88_frames(r)}
        else:
            frame = lambda n: vid.decode_at(r, n)           # noqa: E731
        if diag:                    # (an 8.3 name for the floppy)
            __import__("shutil").copy(pkgf, os.path.join(tmp, "VIDEOD.O88"))
            pkgf = os.path.join(tmp, "VIDEOD.O88")
        disk = os.path.join(tmp, "b.img")
        subprocess.run([sys.executable, "tools/os88disk.py", "-o", disk,
                        "--size", "360", pkgf, av, bv], check=True,
                       capture_output=True)
        with os88ui.boot(img, apps=disk, machine=EMS if a.arm == "layer"
                         else HERC, quick=True) as ui:
            m = ui.m
            S = ui._S
            hsym = {n: m.sym(n) for n in ("mem_base", "mem_top", "spl_live",
                                          "mem_tab")}

            class Q(object):
                def read(self, linear, n):
                    return m.read(linear, n)

            def claims():
                return {c.seg: c for c in heapmap.Map(Q(), hsym).claims}

            def show(what):
                if os.environ.get("VIDCLMOVE_MAP"):
                    heapmap.Map(Q(), hsym).report(what)

            def wait(cond, what, guest=120.0):
                try:
                    os88marty.until(m, cond, what, poll=0.2, limit=900.0,
                                    guest=guest)
                except os88marty.MartyError as e:
                    raise Stop("%s never happened (%s)"
                               % (what, str(e).split(".")[0]))

            class P(object):
                def __init__(self, name):
                    self.w = ui.path("B:/" + name)
                    self.rebase()

                def rebase(self):
                    """the package's REGION moves too (SPEC.md 66.6.1)"""
                    rec = m.read(S("wm_wins") + self.w.i * geom.WIN_SIZE,
                                 geom.WIN_SIZE)
                    self.base = u16(rec, geom.W_SEG) << 4

                def rw(self, n):
                    return u16(m.read(self.base + syms[n], 2))

                def rb(self, n):
                    return m.read(self.base + syms[n], 1)[0]

                def ww(self, n, v):
                    m.write(self.base + syms[n], struct.pack("<H", v))

                def wb(self, n, v):
                    m.write(self.base + syms[n], bytes([v]))

            dg = vid.Geom(vid.LAY_HERC, r.g.wb, 348)

            def hold(p, n, what):
                wait(lambda mm: p.rb("vp_held") == 1 and p.rw("vp_done") == n,
                     "%s (the hold before frame %d)" % (what, n))
                ty0, tx0 = p.rw("vp_ty0"), p.rw("vp_tx0")
                seg = bytes(m.read(0xB0000, 65536))
                got = b"".join(seg[dg.base[ty0 + y] + tx0:
                                   dg.base[ty0 + y] + tx0 + r.g.wb]
                               for y in range(r.g.h))
                d = sum(1 for x, y in zip(got, frame(n - 1)) if x != y)
                print("   %s: the window holds frame %d, %d bytes of %d "
                      "differ" % (what, n - 1, d, len(got)))
                if d:
                    bad.append("%s: frame %d differs in %d bytes"
                               % (what, n - 1, d))

            try:
                # --- the hole-maker: A's block low in the heap, kept
                pa = P("A.V88")
                wait(lambda mm: pa.rb("vp_loaded") == 1, "A's header")
                m.write(pa.base + syms["vp_played"], b"\0")
                m.type_text("p")
                wait(lambda mm: pa.rb("vp_played") == 1, "A to play", 300.0)
                print("   A's block at %04x" % pa.rw("vp_rblk"))
                show("A played (its window %04x)" % (pa.base >> 4))
                # --- B, in its window, to a hold
                pb = P("B.V88")
                wait(lambda mm: pb.rb("vp_loaded") == 1, "B's header")
                bseg = pb.base >> 4
                if a.arm != "resident":
                    pb.ww("vp_kmax", 3)            # (the ring held small)
                if a.arm == "layer":
                    pb.wb("vp_lycv", 1)            # (key 0's yes, kept)
                n1 = 4
                if a.arm == "resident":            # FROM KEY 1: Right picks it
                    s0 = pb.rw("vp_sel")
                    m.key("ArrowRight")
                    wait(lambda mm: pb.rw("vp_sel") != s0, "Right to key 1",
                         20.0)
                    n1 = r.keys[pb.rw("vp_sel")][0] + 4
                pb.ww("vp_stopat", n1)
                m.write(pb.base + syms["vp_held"], b"\0")
                m.write(pb.base + syms["vp_played"], b"\0")
                m.type_text("p")
                wait(lambda mm: pb.rb("vp_ready") == 1, "B's play", 300.0)
                if pb.rb("vp_winm") != 1:
                    raise Stop("B played FULL SCREEN: no window to pause to")
                hold(pb, n1, "before the move")
                ui.mo.click(700, 12)               # DESK: paused, waiting
                wait(lambda mm: pb.rb("vp_ready") == 0 and
                     pb.rb("vp_sess") == 1, "B paused on the desktop")
                words = {"resident": ("vp_rclm",),
                         "layer": ("vp_rclm", "vp_lyseg"),
                         "diag": ("vp_prseg", "vp_rclm")}[a.arm]
                if a.arm == "layer":
                    print("   the layer: own claim %04x, flip %d, in place "
                          "%d, on %d" % (pb.rw("vp_lyseg"), pb.rb("vp_lyb"),
                                         pb.rb("vp_einp"), pb.rb("vp_lyon")))
                    if not pb.rw("vp_lyseg") or pb.rb("vp_lyb") or \
                            not pb.rb("vp_einp"):
                        raise Stop("the layer has no claim of its own here "
                                   "(the fixture is not what the row tests)")
                show("B paused (its window %04x)" % (pb.base >> 4))
                w0 = {n: pb.rw(n) for n in words}
                ring0 = pb.rw("vp_ring")
                held = claims()
                print("   B paused: %s, [vp_ring] %04x" % ("  ".join(
                    "%s=%04x" % kv for kv in w0.items()), ring0))
                krec_para = held[ring0].para if ring0 in held else 0
                hashes = {}
                for n, s in w0.items():
                    c = held.get(s)
                    if c is None:
                        bad.append("%s = %04x names no claim before the move"
                                   % (n, s))
                        continue
                    hashes[n] = hashlib.md5(m.read(s << 4, c.para << 4)
                                            ).digest()
                decl = [n for n, s in w0.items() if s in held and
                        held[s].rloc == syms["vp_smove"]]
                print("   1 declared movable   %d of %d" % (len(decl),
                                                          len(w0)))
                if len(decl) != len(w0):
                    bad.append("1: not declared (vp_smove): %s"
                               % sorted(set(w0) - set(decl)))
                # --- A goes: a hole under B's claims; the posted pass
                # A FRONTED BY ITS DOCK TILE: B's window, laid out after it,
                # may cover all but a sliver of A's title bar, and a click
                # there was seen to raise nothing (os88ui's raise_window
                # takes the tile only when the bar is wholly covered)
                for _ in range(3):
                    if geom.top(m, S) == geom.winptr(m, pa.w.i, S):
                        break
                    ui.mo.click(*geom.tile_xy(m, pa.w.i, S))
                    try:
                        wait(lambda mm: geom.top(m, S) ==
                             geom.winptr(m, pa.w.i, S), "A fronted", 10.0)
                    except Stop:
                        pass
                ui.close(pa.w)
                aseg = pa.base >> 4
                wait(lambda mm: not any(c.own == aseg or c.seg == aseg
                                        for c in claims().values()),
                     "A's claims to be freed")
                wptr = geom.winptr(m, pb.w.i, S)
                # MEM_PG_HIGH: a posted pass sheds only what is CHEAPER than
                # its rank (SPEC.md 66.4.3.3), and a purgeable claim is shed,
                # never moved (18.8.4). At 0xFC (LOW) B:'s FAT window - a MED
                # cache - stood flush under the diag arm's [vp_prseg] at
                # 20000..21400, so the profile had no ground below it and
                # 'did not move' was the kernel being right. HIGH is the rank
                # DOS posts at for that same window (tests/thewire.py's post)
                m.write(S("mem_cpq_lvl"), bytes([0xFE]))
                m.write(S("mem_cpq"), struct.pack("<H", wptr))
                wait(lambda mm: u16(m.read(S("mem_cpq"), 2)) == 0,
                     "the posted compaction", 30.0)
                os88marty.ui_done(m)
                show("after the pass")
                pb.rebase()
                print("   B's region %04x -> %04x" % (bseg, pb.base >> 4))
                bseg = pb.base >> 4
                w1 = {n: pb.rw(n) for n in words}
                ring1 = pb.rw("vp_ring")
                held = claims()
                print("   after: %s, [vp_ring] %04x" % ("  ".join(
                    "%s=%04x" % kv for kv in w1.items()), ring1))
                moved = [n for n in words if w1[n] != w0[n]]
                print("   2 moved              %s"
                      % (", ".join(moved) or "NOTHING"))
                if set(moved) != set(words):
                    bad.append("2: did not move: %s"
                               % sorted(set(words) - set(moved)))
                stale = [n for n, s in w1.items() if s not in held]
                rc = held.get(w1["vp_rclm"])
                if not rc or not rc.seg <= ring1 < rc.end or \
                        ring1 - w1["vp_rclm"] != ring0 - w0["vp_rclm"]:
                    stale.append("vp_ring")
                print("   3 every word live    %s"
                      % ("yes" if not stale else "STALE: %s" % stale))
                if stale:
                    bad.append("3: stale words: %s" % stale)
                ch = [n for n in moved if n in hashes and w1[n] in held and
                      hashlib.md5(m.read(w1[n] << 4, held[w1[n]].para << 4)
                                  ).digest() != hashes[n]]
                print("   4 contents survived  %s"
                      % ("yes" if not ch else "CHANGED: %s" % ch))
                if ch:
                    bad.append("4: contents changed: %s" % ch)
                # --- 5: play on, in the window
                ui.raise_window(next(x for x in ui.windows()
                                     if x.i == pb.w.i))
                n2 = n1 + 25
                pb.ww("vp_stopat", n2)
                m.write(pb.base + syms["vp_held"], b"\0")
                m.type_text("p")
                wait(lambda mm: pb.rb("vp_ready") == 1, "B to play on")
                hold(pb, n2, "5 after the move")
                pb.ww("vp_stopat", 0xFFFF)
                m.write(pb.base + syms["vp_held"], b"\0")
                wait(lambda mm: pb.rb("vp_played") == 1, "B's end", 300.0)
                done, err = pb.rw("vp_done"), pb.rb("vp_err")
                print("   5 played on: drew %d of %d, error %d, late %d"
                      % (done, nf, err, pb.rw("vp_late")))
                if done != nf or err:
                    bad.append("5: drew %d of %d, error %d" % (done, nf, err))
                if a.arm == "layer":
                    # THE LAYER WAS READ out of its moved slots: records
                    # drawn and none left out, and the hold after the move
                    # is one the base alone would not have drawn
                    seen = plain[n2 - 1] != want[n2 - 1]
                    print("   5 the layer: %d records drawn, %d left out, %d "
                          "read too late; the hold after the move is %s"
                          % (pb.rw("vp_lygot"), pb.rw("vp_lyskp"),
                             pb.rw("vp_lymis"), "the LAYERED picture"
                             if seen else "the base's too (no test)"))
                    if not pb.rw("vp_lygot") or pb.rw("vp_lyskp") or \
                            pb.rw("vp_lymis") or not seen:
                        bad.append("5: the layer after the move: %d drawn, "
                                   "%d left out, %d late, visible %s"
                                   % (pb.rw("vp_lygot"), pb.rw("vp_lyskp"),
                                      pb.rw("vp_lymis"), seen))
                wait(lambda mm: pb.rb("vp_sess") == 0, "the session's end")
                if a.arm == "resident":
                    # THE KEY'S RECORD GIVEN BACK: the claim [vp_ring] named
                    # - where it was, or where it moved to - is no claim of
                    # B's after the session (it leaked, in [vp_ring] alone,
                    # before vp_sstart stored it in [vp_rclm] too)
                    os88marty.quiesce(m, lambda: sorted(claims()), guest=2.0,
                                      what="the claims")
                    left = [c for c in claims().values() if c.own == bseg
                            and c.seg in (ring0, ring1)
                            and c.para == krec_para]
                    print("   6 the key's record (%d KB at %04x) after the "
                          "session's end: %s" % (krec_para // 64, ring0,
                                                "STILL HELD" if left
                                                else "given back"))
                    if not krec_para or left:
                        bad.append("6: the key's record was never freed"
                                   if left else "6: no key record seen")
                if diag:
                    os88marty.ui_done(m)
                    m.pause()
                    m.flush(1, os.path.join(tmp, "back.img"))
                    m.run()
                    fat = os88fat.Fat12(os.path.join(tmp, "back.img"))
                    prof = b""
                    for i in range(1, 10):      # B's: A played first and
                        try:                    # wrote one of its own
                            got = bytes(fat.read("VPPROF%d.DAT" % i))
                        except Exception:
                            break
                        if got[16:21] == b"B.V88":
                            prof = got
                    nfr = u16(prof, 6) if prof[:4] == b"VPPR" else -1
                    zero = [i for i in range(max(0, nfr))
                            if not u16(prof, 48 + 4 * i)]
                    print("   6 B's VPPROFn.DAT: %r, %d frames, %d slots zero"
                          % (prof[:4], nfr, len(zero)))
                    if nfr != nf or zero:
                        bad.append("6: the profile holds %d frames, %d of "
                                   "them untimed (%s...)"
                                   % (nfr, len(zero), zero[:6]))
            except (Stop, os88marty.MartyError, os88ui.UIError) as e:
                bad.append(str(e).splitlines()[0])
                if os.environ.get("VIDCLMOVE_SHOT"):
                    fw, fh, px = m.fbuf(None)
                    os88marty.write_png_rgb(os.environ["VIDCLMOVE_SHOT"],
                                            fw, fh, px)
    for x in bad:
        print("   FAIL: %s" % x)
    print("vidclmove %s: %s" % (a.arm, "FAIL" if bad else "ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
