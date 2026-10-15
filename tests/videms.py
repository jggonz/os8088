#!/usr/bin/env python3
"""VIDEO.O88's bank in EXPANDED MEMORY - SPEC.md 98.3.18.6, VIDEO-XMS-PLAN
10.8's wave E2.

    make && make emstest && python3 tests/videms.py

ON MARTYPC'S 8088 WITH A LO-TECH 2 MB EMS BOARD
(`os8088_5150_herc_hdd_sb_ems_gla`), booted from build/emstest.img, whose
SYSTEM.CFG wants EMS.DRV - an 8088 with expanded memory and no XMS, the
machine the EMS bank exists for. Nothing here is timed; every wait is on
guest state.

The instrument is tests/vidxms.py's, a DISK SWAP: the clip's floppy in B: is
changed for a BLANK one once the prefill has filled the bank, so whatever the
play still reads it can only have from the ring and the bank. The ring is
held to three slots (`vp_kmax`, a gate's), so the bank is what carries it.

  1. the file opens with an EMS bank - [vp_xbank] and [vp_xems] 1, slots
     enough for the whole clip;
  2. Play, the gate's hold on the prefill's end (`vp_pfwait`): the box says
     `Buffering 100%`, nothing drawn, the bank holding the clip;
  3. B: blank, Space: held before each of a handful of frames, the Hercules
     screen is the reference decode's, and the play reaches the last frame
     with no error - every chunk after the ring's three came out of the
     board's pages;
  4. Play again with B: still blank: the bank is behind the start, so the
     reader must go to the disk, and the play must NOT reach the end. That
     is what says the swap bites.

VERIFIED TO FAIL: with vp_eopen's ALLOC skipped the player takes no bank
(step 1); with vp_bfill's EMS copy left out the play's pictures differ.
"""
import os
import random
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path[:0] = [HERE, os.path.join(ROOT, "tools")]
import os88build, os88marty, os88ui, os88vid as vid, os88geom as geom  # noqa
from cycweb import pkg_syms                                   # noqa: E402

IMG = "build/emstest.img"
BOARD = "os8088_5150_herc_hdd_sb_ems_gla"
NF, FPS, WB, H = 200, 30.0, 80, 200
STOPS = tuple(int(x) for x in os.environ.get("VIDEMS_STOPS", "40,100,160").split(","))
VOK_BUF = 10


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


def clip(tmp):
    """200 canvases 80 x 200 on the Hercules layout, ~1.2 KB of change a
    frame: ~250 KB, eight chunks - three for the ring and the rest the bank's,
    on a 360 KB floppy"""
    rnd = random.Random(1077)
    cv = bytearray(WB * H)
    paths = []
    for f in range(NF):
        for _ in range(100):
            a = rnd.randrange(WB * H - 8)
            cv[a:a + 8] = bytes(rnd.getrandbits(8) for _ in range(8))
        p = os.path.join(tmp, "f%03d.pbm" % f)
        vid._write_pbm(p, WB, H, bytes(cv))
        paths.append(p)
    out = os.path.join(tmp, "CLIP.V88")
    vid.encode_frames(paths, out, FPS, None, "herc", "videms clip")
    vid.verify_v88(out)
    return out


def main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--hybrid", action="store_true",
                    help="SPEC.md 98.3.18.6's hybrid (vp_noinp): a FIFO ahead "
                         "of a conventional ring, copied down - where the "
                         "default is 98.3.18.7's decode IN PLACE")
    ap.add_argument("--wrap", action="store_true",
                    help="in place, the ring held to 3 of the board's slots "
                         "(vp_ekr): it WRAPS, and the reader fills quarter 3 "
                         "while the hook reads 0-2 - so B: stays, the disk "
                         "being what the play reads on from")
    ap.add_argument("--slow", action="store_true",
                    help="SPEC.md 98.3.18.8: the board measured SLOW "
                         "(vp_eslow 1, poked over what MartyPC's board "
                         "measures) - so the session takes the hybrid and "
                         "never decodes in place, the decode reading every "
                         "record through the slow board")
    a = ap.parse_args()
    a.hybrid = a.hybrid or a.slow
    os.chdir(ROOT)
    for p in (IMG, "build/video.o88"):
        if not os.path.exists(os88build.at(p)):
            sys.exit("videms: no %s - run `make && make emstest`" % p)
    syms, image = pkg_syms("apps/video/video.asm", ("apps/",))
    bad = []
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        v88 = clip(tmp)
        r = vid.Reader(v88)
        size = os.path.getsize(v88)
        print("   clip: %d frames, %d bytes (%d chunks)"
              % (r.frames, size, -(-size // vid.SLOT)))
        disk = os.path.join(tmp, "videms.img")
        blank = os.path.join(tmp, "blank.img")
        for out, files in ((disk, [os88build.at("build/video.o88"), v88]),
                           (blank, [])):
            subprocess.run([sys.executable, "tools/os88disk.py", "-o", out,
                            "--size", "360"] + files, check=True,
                           capture_output=True)
        tg = vid.Geom(vid.LAY_HERC, WB, 348)
        ty0 = (348 - H) // 2 // tg.banks * tg.banks
        tx0 = (tg.stride - WB) // 2
        rows_at = [tg.base[y + ty0] + tx0 for y in range(H)]
        with os88ui.boot(os88build.at(IMG), apps=disk, machine=BOARD) as ui:
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

            def state():
                return ("done=%d err=%d ready=%d played=%d lc=%d pc=%d k=%d "
                        "kr=%d inp=%d ems=%d bn=%d cnt=%d hs=%d beof=%d"
                        % (rw("vp_done"), rb("vp_err"), rb("vp_ready"),
                           rb("vp_played"), rw("vp_lc"), rw("vp_pc"),
                           rw("vp_k"), rw("vp_kr"), rb("vp_einp"),
                           rb("vp_xems"), rw("vp_xbn"), rw("vp_xcnt"),
                           rw("vp_xhs"), rb("vp_xbeof")))

            def until(cond, what, guest=120.0):
                try:
                    os88marty.until(m, lambda mm: cond(), what, poll=0.2,
                                    limit=900.0, guest=guest)
                except os88marty.MartyError:
                    print("   TIMED OUT: %s\n     %s" % (what, state()))
                    if os.environ.get("VIDEMS_DBG"):
                        d = open(v88, "rb").read()
                        fr = bytes(m.read(0xE0000, 65536))
                        for q in range(4):
                            blk = fr[q * 16384:q * 16384 + 64]
                            print("     quarter %d: file offset %s" % (q, [
                                i for i in range(0, len(d) - 64, 512)
                                if d[i:i + 64] == blk][:3]))
                        print("     pc %d po %d rofs %d psec %d"
                              % (rw("vp_pc"), rw("vp_po"), rw("vp_rofs"),
                                 rw("vp_psec")))
                    if rb("vp_err"):
                        em = m.read(base + rw("vp_errmsg"), 48)
                        print("     its error: %r"
                              % em.split(b"\0")[0].decode("ascii", "replace"))
                    raise

            until(lambda: rb("vp_loaded") == 1, "the clip's header", 30.0)
            # (the bank is opened AFTER the header is loaded - and since
            # 98.3.18.8 the board's speed is measured first, ~6 ticks)
            until(lambda: rb("vp_eslow") != 0xFF, "the board measured", 30.0)
            try:
                until(lambda: rb("vp_xbank") == 1, "the EMS bank", 5.0)
            except Exception:
                pass                # (said with its numbers below)
            if rb("vp_ok") != 1:
                sys.exit("videms: the player will not play the clip here")
            # --- 1: an EMS bank
            bn = rw("vp_xbn")
            print("   opened: xbank %d, ems %d, %d slots (%d KB), frame %04x"
                  % (rb("vp_xbank"), rb("vp_xems"), bn, bn * 32,
                     rw("vp_eseg")))
            if rb("vp_xbank") != 1 or rb("vp_xems") != 1:
                sys.exit("videms: no EMS bank was taken (%s; the board "
                         "measured %d tenths of RAM's time, slow %d)"
                         % (state(), rw("vp_er10"), rb("vp_eslow")))
            # THE BOARD'S SPEED (98.3.18.8): measured at the open, against
            # this package's RAM - MartyPC's board is RAM's speed
            print("   the board measured: %d.%d x RAM's time, slow %d"
                  % (rw("vp_er10") // 10, rw("vp_er10") % 10,
                     rb("vp_eslow")))
            if rb("vp_eslow") != 0 or not 5 <= rw("vp_er10") <= 15:
                bad.append("MartyPC's board measured %d tenths of RAM's "
                           "time (slow %d), not RAM's own"
                           % (rw("vp_er10"), rb("vp_eslow")))
            if a.slow:
                m.write(base + syms["vp_eslow"], b"\1")
            if bn * vid.SLOT < size:
                bad.append("the bank is %d KB for a file of %d"
                           % (bn * 32, size // 1024))
            m.write(base + syms["vp_nowin"], b"\1")     # the full screen
            m.write(base + syms["vp_noinp"],
                    b"\1" if a.hybrid and not a.slow else b"\0")
            ww("vp_ekr", 3 if a.wrap else 0)
            ww("vp_kmax", 3)                            # the ring held small
            m.write(base + syms["vp_pfwait"], b"\1")
            ww("vp_stopat", STOPS[0])
            # --- 2: the prefill, held at its end
            m.type_text("p")
            def filled():                   # the prefill's own count at its
                n = rw("vp_lc") if rb("vp_einp") else rw("vp_xcnt")
                return rb("vo_kind") == VOK_BUF and rw("vp_pfn") and \
                    n >= rw("vp_pfn")       # target: the file's end, the
            until(filled, "the prefill to bank the clip", 300.0)  # ring's
            inp = rb("vp_einp")
            print("   in place %d: the ring %d slots (the conventional %d)"
                  % (inp, rw("vp_kr"), rw("vp_k")))
            if inp != (0 if a.hybrid else 1):
                bad.append("the session played %s, the %s was asked"
                           % ("IN PLACE" if inp else "the hybrid",
                              "hybrid" if a.hybrid else "decode in place"))
            txt = m.read(base + syms["vo_text"], 20).split(b"\0")[0].rstrip()
            print("   prefilled: the box says %r, ready %d; %s"
                  % (txt.decode("ascii", "replace"), rb("vp_ready"),
                     state()))
            if txt != b"Buffering 100%" or rb("vp_ready"):
                bad.append("the prefill's box said %r, ready %d"
                           % (txt, rb("vp_ready")))
            if rw("vp_k") > 3:
                bad.append("the ring is %d slots, not the gate's 3"
                           % rw("vp_k"))
            # --- 3: B: blank, Space, and the play out of the bank (or, the
            # ring wrapping, off the disk as it plays)
            if not a.wrap:
                m.mount(1, blank)
            elif rw("vp_kr") != 3:
                bad.append("the in-place ring is %d slots, not the gate's 3"
                           % rw("vp_kr"))
            m.type_text(" ")
            for i, n in enumerate(STOPS):
                until(lambda: rb("vp_held") == 1 and rw("vp_done") == n,
                      "the hold before frame %d" % n, 120.0)
                seg = bytes(m.read(0xB0000, 65536))
                got = b"".join(seg[b:b + WB] for b in rows_at)
                want = vid.decode_at(r, n - 1)
                diff = sum(1 for j in range(len(got)) if got[j] != want[j])
                print("   hold before frame %3d: %d bytes of %d differ  (%s)"
                      % (n, diff, len(got), state()))
                if diff:
                    bad.append("the screen before frame %d differs in %d "
                               "bytes" % (n, diff))
                ww("vp_stopat", STOPS[i + 1] if i + 1 < len(STOPS)
                   else 0xFFFF)
                m.write(base + syms["vp_held"], b"\0")
            until(lambda: rb("vp_played") == 1 and rb("vp_ready") == 0,
                  "the play to end", 120.0)
            print("   the play%s: %s" % ("" if a.wrap else ", B: blank",
                                         state()))
            if rb("vp_err"):
                em = m.read(base + rw("vp_errmsg"), 48).split(b"\0")[0]
                print("   ...its error: %r" % em.decode("ascii", "replace"))
            if rw("vp_done") != NF or rb("vp_err"):
                bad.append("the play out of the bank drew %d of %d (error %d)"
                           % (rw("vp_done"), NF, rb("vp_err")))
            # THE CARD SAYS WHICH (98.3.18.6): line 5 after a play names
            # the bank it went through - the field's question, unanswered
            # by the card until it did
            kind = 2 if a.hybrid else 3
            if rb("vp_lbk") != kind:
                bad.append("the card's bank after the play is kind %d, not "
                           "%d (%s)" % (rb("vp_lbk"), kind,
                                        "EMS" if a.hybrid else
                                        "EMS in place"))
            if a.wrap:
                print("   the ring wrapped: %d chunks through 3 slots"
                      % rw("vp_lc"))
                if rw("vp_lc") <= 3:
                    bad.append("the ring never wrapped (%d chunks)"
                               % rw("vp_lc"))
                return report(bad)
            # --- 4: again, B: still blank - the bank is behind the start
            m.write(base + syms["vp_played"], b"\0")
            m.write(base + syms["vp_pfwait"], b"\0")
            m.type_text("p")
            until(lambda: rb("vp_played") == 1 and rb("vp_ready") == 0,
                  "the second play to stop", 180.0)
            print("   the second play, B: blank: %s" % state())
            if rw("vp_done") == NF and not rb("vp_err"):
                bad.append("with B: blank and the bank behind the start the "
                           "play STILL reached the end - the swap does not "
                           "bite")
    return report(bad)


def report(bad):
    if bad:
        print("\nvidems: FAIL")
        for b in bad:
            print("  - " + b)
        return 1
    print("\nvidems: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
