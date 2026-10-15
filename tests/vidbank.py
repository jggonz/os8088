#!/usr/bin/env python3
"""VIDEO.O88's XMS BANK: a streamed .V88 BIGGER than the pool, played with a
FIFO of chunks in XMS ahead of the ring - docs/plans/VIDEO-XMS-PLAN.md 4,
SPEC.md 98.3.18.2.

    make && python3 tests/vidbank.py

WHY QEMU: docs/TESTING.md's closed list, entry 1 - XMS is a 286-and-up store
and every MartyPC machine is an 8088. `-m 2` gives the machine 1 MB above the
first, and the clip is ~1.35 MB, so the whole-file hold (98.3.18) cannot be
taken and the player must bank instead. Nothing here is timed; every wait is
on guest state.

The clip is LIN80, 640 x 200, so the bracket writes it straight to A000 at
the centred origin and the picture is read back off the card (plane 0: a
MONO1 byte goes to all four, SPEC.md 98.1.2) at the gate's holds
(`vp_stopat`, tests/vidplay.py's), against the reference decode.

LEG play (B: the clip's disk throughout):
  1. the file opens with a BANK - [vp_xbank] 1, [vp_xbn] slots, nothing of
     the file held ([vp_xhave] 0);
  2. Play, held before frame 20: with the hook stopped the ring fills and
     then THE BANK DOES, to every slot it has - the reader's idle time;
  3. held again before each key frame: the picture is the decode's, so the
     chunks came down out of the bank in order and whole;
  4. the play runs to the last frame with no error.
LEG swap (the instrument is tests/vidxms.py's, a disk swap):
  5. Play again, held before frame 20 until the bank is full - then B: is
     changed to a BLANK floppy under the running player;
  6. the hold moves to the furthest key frame whose data lies inside what the
     ring and the bank had between them, and THAT FRAME IS RIGHT - every byte
     after the ring's came out of XMS, there being no disk to read;
  7. released, the play must NOT reach the end: what neither held is the
     blank disk's. That is what says the swap bites.

Step 1 also asserts the bank is the WHOLE pool (98.3.18.2.1): 26 slots,
where the 256 KB VP_XRES kept back for others left 18 - put back, it FAILS.

VERIFIED TO FAIL: with `call vp_bstep` taken out of the reader's loop the
bank stays empty (step 2), and with vp_bfill's head left unmoved step 3's
picture differs.
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
import os88qemu                                             # noqa: E402
import os88build                                            # noqa: E402
import os88geom as geom                                     # noqa: E402
import os88ui                                               # noqa: E402
import os88vid as vid                                       # noqa: E402
import xmcheck                                              # noqa: E402
from cycweb import pkg_syms                                 # noqa: E402
from vidxms import Q, u16, u32, live_clip                   # noqa: E402
from pxswin import qpoke                                     # noqa: E402

NF, FPS, WB, H = 300, 30.0, 80, 200
HOLD0 = 20
BPS = 64 * 1024                 # B:'s throttle: a slow period disk
ASK_KB, PRE_KB = 4096, 128          # the header's bank and prefill: a
                                    # small prefill, so the pause leg has
                                    # the bank's room to watch filling -
                                    # and four slots, not two: the box's
                                    # "~Ns" shows only between the first
                                    # slot and the last, and with two that
                                    # was one half-second step a poll could
                                    # miss (it failed 2 runs in 4)
VOK_LOWMEM = 7
VP_SPK = 2                      # [vp_snd]: the speaker plays it
VOK_BUF = 10                    # vosd.inc: the box says the prefill
CHUNK = 32768


def clip(tmp, sound=False, ybase=0):
    """300 frames of 640 x 200 at ~4.5 KB of change each: ~1.35 MB, which
    is more than `-m 2` has above 1 MB and less than a 1.44 MB floppy"""
    import random
    rnd = random.Random(5088)
    cv = bytearray(WB * H)
    paths = []
    for f in range(NF):
        for _ in range(400 if sound else 490):
            a = rnd.randrange(WB * H - 8)
            cv[a:a + 8] = bytes(rnd.getrandbits(8) for _ in range(8))
        p = os.path.join(tmp, "f%03d.pbm" % f)
        vid._write_pbm(p, WB, H, bytes(cv))
        paths.append(p)
    out = os.path.join(tmp, "CLIP.V88")
    wav = None
    if sound:                   # (the speaker's arm: PCM8, and no card)
        import math
        rate = 8000
        wav = os.path.join(tmp, "a.wav")
        vid._write_wav(wav, rate, bytes(
            128 + int(90 * math.sin(2 * math.pi * 440.0 * i / rate))
            for i in range(int(NF / FPS * rate))))
    vid.encode_frames(paths, out, FPS, wav, "lin80", "vidbank clip",
                      audio_fmt=vid.AUD_PCM8)
    # THE FILE'S ASK (98.3.18.3), as --bank 4096 --prefill would write it:
    # a bank bigger than this machine's, and a prefill of PRE_KB
    d = bytearray(open(out, "rb").read())
    struct.pack_into("<HH", d, vid.H_XBANK, ASK_KB, PRE_KB)
    struct.pack_into("<H", d, vid.H_YBASE, ybase)   # (98.3.18.9's yardstick)
    open(out, "wb").write(d)
    vid.verify_v88(out)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--arm", choices=("fs", "win", "spk", "spkbank",
                                      "spkfast"), default="fs",
                    help="fs: the full screen's legs; win: the prefill in "
                         "the WINDOW, where the thumb is its meter; spk: "
                         "the PC SPEAKER as the clock, under which no XMS "
                         "copy may run; spkbank: the file says the machine "
                         "it was made for is this one, and the BANK wins - "
                         "muted, banked (98.3.18.9); spkfast: it says a "
                         "machine far slower, and the speaker stays")
    ap.add_argument("--bps", type=int, default=BPS,
                    help="B:'s throttle, bytes a second (0 none): slow "
                         "enough that the bank is seen filling")
    ap.add_argument("--mem", type=int, default=2,
                    help="QEMU -m (MB): the pool is what is above the first")
    a = ap.parse_args()
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/video/video.asm", ("apps/",))
    pkg = os88build.at("build/video.o88")
    sysimg0 = os88build.at("build/os8088.img")
    for p in (pkg, sysimg0):
        if not os.path.exists(p):
            sys.exit("vidbank: no %s - run `make`" % p)
    bad = []
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        v88 = live_clip(tmp) if a.arm == "win" else \
            clip(tmp, sound=a.arm.startswith("spk"),
                 ybase={"spkbank": 1, "spkfast": 0xFFFF}.get(a.arm, 0))
        size = os.path.getsize(v88)
        rd88 = vid.Reader(v88)
        tg = vid.Geom(vid.LAY_LIN80, WB, 480)
        ty0 = (480 - H) // 2 // tg.banks * tg.banks
        row0 = tg.base[ty0]
        print("   clip: %d frames, %d bytes (%d KB), %d keys"
              % (rd88.frames, size, size // 1024, len(rd88.keys)))
        disk = os.path.join(tmp, "vidbank.img")
        blank = os.path.join(tmp, "blank.img")
        sysimg = os.path.join(tmp, "sys.img")
        shutil.copy(sysimg0, sysimg)
        for out, files in ((disk, [v88, pkg]), (blank, [])):
            subprocess.run([sys.executable, "tools/os88disk.py", "-o", out,
                            "--size", "1440"] + files, check=True,
                           capture_output=True)
        q = Q(tmp, sysimg, disk, a.mem, bps=a.bps)
        for _ in range(150):
            if os.path.exists(q.sock):
                break
            time.sleep(0.2)
        xmcheck.wait_desktop(q.sock, "vidbank")
        S = xmcheck.sym
        ui = os88ui.UI(q, mouse=object(), sym=S, verbose=False)

        def wait(cond, what, secs):
            if not os88qemu.acted(q, cond, secs=secs, what=what, poll=0.05):
                raise SystemExit("vidbank: timed out waiting for %s (%s, "
                                 "bank seeks %d)" % (what, state(),
                                                     rw("vp_bskn")))

        x, y = geom.drive_pt(q, "B", S)
        xmcheck.dblclick(q.sock, x, y)
        box = {}

        def diskwin():
            for w in ui.windows():
                if w.visible and ui.fs_of(w) == (1, 0, 1):
                    box["w"] = w
                    return True
            return False
        if not os88qemu.acted(q, diskwin, secs=30, what="drive B",
                              poll=0.05):
            sys.exit("vidbank: drive B's window did not open")
        os88qemu.pace(q, 1)
        w = box["w"]
        i, _ = ui.entry("CLIP.V88", w)
        before = set(o.i for o in ui.windows())
        xmcheck.dblclick(q.sock, *ui.row_xy(w, i - ui.scroll(w)))

        def player():
            for o in ui.windows():
                if o.i not in before and o.visible:
                    box["p"] = o
                    return True
            return False
        if not os88qemu.acted(q, player, secs=30, what="the player",
                              poll=0.05):
            sys.exit("vidbank: the Video Player's window did not open")
        rec = q.read(S("wm_wins") + box["p"].i * geom.WIN_SIZE, geom.WIN_SIZE)
        base = u16(rec, geom.W_SEG) << 4

        def rb(n):
            return q.read(base + syms[n], 1)[0]

        def rw(n):
            return u16(q.read(base + syms[n], 2))

        def rd(n):
            return u32(q.read(base + syms[n], 4))

        def state():
            return ("done=%d err=%d ready=%d played=%d lc=%d pc=%d k=%d "
                    "bank=%d xon=%d bn=%d cnt=%d hs=%d beof=%d held=%d"
                    % (rw("vp_done"), rb("vp_err"), rb("vp_ready"),
                       rb("vp_played"), rw("vp_lc"), rw("vp_pc"),
                       rw("vp_k"), rb("vp_xbank"), rb("vp_xon"),
                       rw("vp_xbn"), rw("vp_xcnt"), rw("vp_xhs"),
                       rb("vp_xbeof"), rb("vp_held")))

        def poke(n, v):
            qpoke(q, [(base + syms[n], struct.pack("<H", v & 0xFFFF))])

        if not os88qemu.acted(q, lambda: rb("vp_loaded") == 1, secs=30,
                              what="the clip's header", poll=0.05):
            sys.exit("vidbank: the clip did not open")
        if rb("vp_ok") != 1:
            sys.exit("vidbank: the player will not play the clip here")

        # --- 1: a bank, not a hold
        print("   opened: %s xhave=%d" % (state(), rd("vp_xhave")))
        if rb("vp_xbank") != 1 or rb("vp_xon") != 1:
            sys.exit("vidbank: no bank was taken (%s)" % state())
        bn = rw("vp_xbn")
        if bn < 4 or rd("vp_xhave"):
            bad.append("the bank is %d slots, %d bytes held"
                       % (bn, rd("vp_xhave")))
        print("   the bank: %d slots, %d KB" % (bn, bn * 32))
        # THE WHOLE POOL (98.3.18.2.1): -m 2's is banked in 26 slots; the
        # 256 KB once kept back for others left 18
        if bn < 26:
            bad.append("the bank is %d slots of the pool's 26: some of it "
                       "was kept back" % bn)

        def screen():
            raw = q.read(0xA0000 + row0, WB * H)
            return b"".join(raw[tg.base[ty0 + yy] - row0:
                                tg.base[ty0 + yy] - row0 + WB]
                            for yy in range(H))

        def hold_at(n, what, secs=60):
            wait(lambda: rb("vp_held") == 1 and rw("vp_done") == n,
                 "the hold before frame %d (%s)" % (n, what), secs)
            wait(lambda: rb("vo_on") == 0, "the box to come down", 30)

        def check(n):
            got = screen()
            want = vid.decode_at(rd88, n - 1)
            diff = sum(1 for j in range(len(got)) if got[j] != want[j])
            print("   hold before frame %3d: %d bytes of %d differ  (%s)"
                  % (n, diff, len(got), state()))
            if diff:
                bad.append("the screen before frame %d differs in %d bytes"
                           % (n, diff))

        def release(nxt):
            qpoke(q, [(base + syms["vp_stopat"], struct.pack("<H", nxt)),
                      (base + syms["vp_held"], b"\0")])

        if a.arm in ("spkbank", "spkfast"):
            # --- THE SPEAKER AGAINST THE BANK (98.3.18.9): the file's
            # yardstick says the machine it was made for decodes its key in
            # 10 us (spkbank: this machine is not twice that fast - QEMU
            # takes ~2 ms) or in 655 ms (spkfast: it is). The bench needs a key, so the play is
            # from key 1 - Right first; spkbank also plays from frame 0,
            # where there is no key to time and the bank wins regardless
            fast = a.arm == "spkfast"
            for leg in (("key 1",) if fast else ("frame 0", "key 1")):
                if leg == "key 1":
                    s0 = rw("vp_sel")
                    q.hmp("sendkey right")
                    wait(lambda: rw("vp_sel") != s0, "Right to key 1", 20)
                qpoke(q, [(base + syms["vp_played"], b"\0")])
                q.hmp("sendkey p")
                wait(lambda: rb("vp_ready") == 1, "the play to start", 120)
                wait(lambda: rw("vp_done") >= rw("vp_base") + 30,
                     "30 frames", 60)
                q.hmp("stop")
                st = (rb("vp_snd"), rb("vp_mute"), rb("vp_mwhy"),
                      rw("vp_lytm"), rw("vp_ytb"), rb("vp_lbk"))
                q.hmp("cont")
                print("   from %s: snd %d, mute %d why %d; bench %d against "
                      "%d; the bank kind %d (%s)" % ((leg,) + st + (state(),)))
                want = (VP_SPK, 0) if fast else (0, 1)
                if st[:2] != want:
                    bad.append("from %s the play's sound is %d, muted %d - "
                               "not %r" % ((leg,) + st[:2] + (want,)))
                if not fast and (st[2] != 3 or st[5] != 1):
                    bad.append("from %s the mute's reason is %d and the bank "
                               "kind %d, not 3 and XMS" % (leg, st[2], st[5]))
                if leg == "key 1" and not st[3]:
                    bad.append("from key 1 the bench timed nothing")
                q.hmp("sendkey esc")
                wait(lambda: rb("vp_ready") == 0 and rb("vp_played") == 1,
                     "Esc", 60)
                qpoke(q, [(base + syms["vp_mute"], b"\0"),
                          (base + syms["vp_umute"], b"\0"),
                          (base + syms["vp_mwhy"], b"\0")])
            q.close()
            if bad:
                print("\nvidbank (%s): FAIL" % a.arm)
                for b in bad:
                    print("  - " + b)
                return 1
            print("\nvidbank (%s): ok" % a.arm)
            return 0

        if a.arm == "spk":
            # --- THE SPEAKER'S CLOCK (98.3.18.5): no card here, so a PCM8
            # play is the speaker's, whose clock is its own interrupts - and
            # every XMS copy holds them off. No prefill, and the bank stays
            # empty while it plays; the disk serves
            q.hmp("sendkey p")
            wait(lambda: rb("vp_ready") == 1, "the play to start", 60)
            wait(lambda: rw("vp_done") >= 120, "frame 120", 60)
            q.hmp("stop")
            st = (rb("vp_snd"), rw("vp_pfn"), rw("vp_xcnt"), rw("vp_lc"))
            q.hmp("cont")
            print("   the speaker playing: snd %d, prefill %d, the bank %d, "
                  "%d chunks read (%s)" % (st + (state(),)))
            if st[0] != VP_SPK:
                bad.append("the play is not the speaker's (snd %d)" % st[0])
            if st[1] or st[2]:
                bad.append("under the speaker's clock the bank took %d "
                           "slots and the prefill %d" % (st[2], st[1]))
            most = [st[2]]              # the bank's count, ALL THROUGH: a
                                        # chunk banked is handed straight
            def ended():                # back by the next fill, so one look
                most[0] = max(most[0], rw("vp_xcnt"))   # can read 0 with
                return rb("vp_ready") == 0 and \
                    rb("vp_played") == 1    # copies running all the while
            wait(ended, "the play to end", 120)
            print("   the play: %s; the bank's most %d" % (state(), most[0]))
            if most[0]:
                bad.append("under the speaker's clock the bank held %d "
                           "slots at once" % most[0])
            if rb("vp_lbk") != 5:       # the card: 'XMS bank idle: the
                bad.append("the card's bank is kind %d, not 5 (idle for "
                           "the speaker)" % rb("vp_lbk"))     # speaker'

            if rw("vp_done") != NF or rb("vp_err"):
                bad.append("the speaker's play drew %d of %d (error %d)"
                           % (rw("vp_done"), NF, rb("vp_err")))
            q.close()
            if bad:
                print("\nvidbank (spk): FAIL")
                for b in bad:
                    print("  - " + b)
                return 1
            print("\nvidbank (spk): ok")
            return 0

        if a.arm == "win":
            # --- THE WINDOW (98.3.18.4): a 160 x 120 stream the window
            # hosts, bigger than the pool. The full screen's box is not the
            # window's, so the THUMB is the prefill's meter: it crosses the
            # bar as the bank fills, and the first frame puts it back
            x0 = rw("vp_wtx")
            q.hmp("sendkey p")
            xs = []

            def meter():
                if rb("vp_winm") == 1 and rb("vp_ready") == 0 and \
                        rw("vp_pfn"):
                    xs.append(rw("vp_wtx"))
                return rb("vp_ready") == 1
            wait(meter, "the play to start in the window", 120)
            q.hmp("stop")
            st = (rb("vp_winm"), rw("vp_pfn"), rw("vp_xcnt"), rw("vp_lbw"))
            q.hmp("cont")
            ok = os88qemu.acted(q, lambda: rw("vp_wtx") < max(xs + [0]),
                                secs=30, what="the thumb back", poll=0.05)
            print("   in the window %d: the prefill %d slots, the bank %d; "
                  "the thumb %d -> %d (%d samples) on a bar of %d, back to "
                  "%d" % (st[0], st[1], st[2], x0, max(xs + [0]), len(xs),
                          st[3], rw("vp_wtx")))
            if st[0] != 1 or not st[1] or st[2] < st[1]:
                bad.append("the window's play: window %d, prefill %d, bank "
                           "%d" % st[:3])
            if len(xs) < 3 or max(xs) < st[3] // 2:
                bad.append("the thumb did not cross the bar as the bank "
                           "filled: %r" % xs[-8:])
            if not ok:
                bad.append("the first frame did not put the thumb back")
            q.hmp("sendkey esc")
            q.close()
            if bad:
                print("\nvidbank (win): FAIL")
                for b in bad:
                    print("  - " + b)
                return 1
            print("\nvidbank (win): ok")
            return 0

        # --- 2: THE PREFILL fills the bank before frame 0 (98.3.18.3): the
        # file asks 256 KB, and the box says how far and how long, while
        # B: reads at a period disk's rate
        import re
        poke("vp_stopat", HOLD0)
        # (the bank HELD at the prefill's slots until the pause: F can take
        # guest seconds to land, and the bank must still have room then)
        qpoke(q, [(base + syms["vp_bhold"], b"\1")])
        q.hmp("sendkey p")
        said = set()

        def filling():
            if rb("vo_kind") == VOK_BUF:
                said.add(q.read(base + syms["vo_text"], 20).split(b"\0")[0].rstrip())
            return rb("vp_ready") == 1
        wait(filling, "the play to start", 120)
        q.hmp("stop")
        pfn, cnt0, done0 = rw("vp_pfn"), rw("vp_xcnt"), rw("vp_done")
        ask = (rw("vp_hxb"), rw("vp_hxp"))
        toast = rb("vo_toast")
        q.hmp("cont")
        print("   the first frame due: the file asks %d KB, %d first; the "
              "prefill %d slots, the bank has %d, %d drawn, toast %d"
              % (ask + (pfn, cnt0, done0, toast)))
        print("   the box said: %s" % ", ".join(
            sorted(t.decode("ascii", "replace") for t in said)))
        if ask != (ASK_KB, PRE_KB) or pfn != PRE_KB // 32 or cnt0 < pfn:
            bad.append("the file's ask %r: the prefill asked %d slots of %d "
                       "and the bank had %d at the first frame"
                       % (ask, pfn, bn, cnt0))
        if toast != VOK_LOWMEM:
            bad.append("a bank of %d KB under the file's %d did not say Low "
                       "memory (toast %d)" % (bn * 32, ASK_KB, toast))
        if not any(re.match(rb"Buffering \d{1,2}% ~\d+[sm]$", t)
                   for t in said):
            bad.append("the box never said how far and how long: %r"
                       % sorted(said))

        # --- 3: F to the desktop, PAUSED: the window's timer banks on
        # (98.3.18.4) - the clip cannot play in the window, so the session
        # waits in it
        wait(lambda: rb("vp_held") == 1 and rw("vp_done") == HOLD0,
             "the hold before frame %d" % HOLD0, 60)
        for _ in range(4):              # (pressed again only while the
            if rb("vp_ready") == 0:     # bracket still runs: a key can be
                break                   # lost, and a second press there
            q.hmp("sendkey f")          # cannot toggle back)
            os88qemu.acted(q, lambda: rb("vp_ready") == 0, secs=2,
                           what="F", poll=0.05)
        wait(lambda: rb("vp_sess") == 1 and rb("vp_ready") == 0,
             "the session paused on the desktop", 30)
        c0 = rw("vp_xcnt")
        qpoke(q, [(base + syms["vp_bhold"], b"\0")])
        if c0 >= bn:
            bad.append("the bank was full before the pause - the leg "
                       "proves nothing (%s)" % state())
        else:
            ok = os88qemu.acted(q, lambda: rw("vp_xcnt") > c0 + 1, secs=30,
                                what="the bank to fill paused", poll=0.1)
            print("   paused on the desktop: the bank %d -> %d slots (%s)"
                  % (c0, rw("vp_xcnt"), state()))
            if not ok:
                bad.append("paused on the desktop, the bank stayed at %d of "
                           "%d" % (c0, bn))
        wait(lambda: rw("vp_xcnt") == bn or rb("vp_xbeof"),
             "the bank to fill while paused", 120)
        # --- 4: Play again: on from where it paused, the bank's chunks first
        release(60)
        q.hmp("sendkey p")
        hold_at(60, "after the pause")
        check(60)
        wait(lambda: rw("vp_xcnt") == bn or rb("vp_xbeof"),
             "the bank to fill behind the hold", 120)
        # --- 5: A SEEK INTO THE BANK: Right, held at frame 60. The player
        # counts the presses it got (a QEMU key can repeat) and goes five
        # seconds a press on from the frame on the glass, to the last key at
        # or before - whose super-packet is in the bank, so the ring is
        # handed the bank's chunk under it and nothing is read again. The
        # target is read off the player in the half second before it seeks
        q.hmp("stop")
        xlo0, cnt1 = rd("vp_xlo"), rw("vp_xcnt")
        q.hmp("cont")
        q.hmp("sendkey right")
        wait(lambda: rb("vo_skp") == 1, "the seek's press", 10)
        q.hmp("stop")
        skn = struct.unpack("<h", q.read(base + syms["vp_skn"], 2))[0]
        skb = rw("vp_skb")
        to = min(skb + skn * 5 * int(FPS), NF - 1)
        kf = [k for k in rd88.keys if k[0] <= to][-1]
        stop = next((k[0] for k in rd88.keys if k[0] > kf[0]), NF - 1)
        qpoke(q, [(base + syms["vp_stopat"], struct.pack("<H", stop))])
        q.hmp("cont")
        inbank = xlo0 <= kf[3] < xlo0 + cnt1 * CHUNK
        print("   the seek: %d press(es) from frame %d -> key %d, its "
              "super-packet at %d; the bank holds %d..%d (%s)"
              % (skn, skb, kf[0], kf[3], xlo0, xlo0 + cnt1 * CHUNK,
                 "inside" if inbank else "OUTSIDE"))
        if not inbank:
            bad.append("key %d is not inside the bank - the seek leg "
                       "proves nothing" % kf[0])
        hold_at(stop, "after the seek", 120)
        check(stop)
        print("   seeks the bank answered: %d" % rw("vp_bskn"))
        if rw("vp_bskn") != 1:
            bad.append("the seek into the bank was answered %d times by it"
                       % rw("vp_bskn"))
        # --- 6: to the end
        release(0xFFFF)
        wait(lambda: rb("vp_ready") == 0 and rb("vp_played") == 1,
             "the first play to end", 180)
        print("   first play: %s" % state())
        if rb("vp_lbk") != 1:           # the card: 'Banked in XMS, ...'
            bad.append("the card's bank is kind %d, not 1 (XMS)"
                       % rb("vp_lbk"))
        if rw("vp_done") != NF or rb("vp_err"):
            bad.append("the play through the bank drew %d of %d (error %d)"
                       % (rw("vp_done"), NF, rb("vp_err")))

        def prefilled(what):
            """Play with the gate's hold on the prefill's end: the bank
            full, the box saying so, and no frame drawn yet"""
            qpoke(q, [(base + syms["vp_played"], b"\0"),
                      (base + syms["vp_pfwait"], b"\1"),
                      (base + syms["vp_hxp"], b"\xff\xff"),
                      (base + syms["vp_stopat"], struct.pack("<H", HOLD0))])
            q.hmp("sendkey p")
            wait(lambda: rb("vo_kind") == VOK_BUF and rw("vp_xcnt") == bn,
                 what, 120)
            q.hmp("stop")
            txt = q.read(base + syms["vo_text"], 20).split(b"\0")[0].rstrip()
            st = (txt, rb("vo_on"), rb("vp_ready"), rw("vp_done"))
            q.hmp("cont")
            print("   %s: the box says %r (on %d), ready %d, %d drawn"
                  % (what, txt.decode("ascii", "replace"), st[1], st[2],
                     st[3]))
            if st != (b"Buffering 100%", 1, 0, 0):
                bad.append("the prefill's box was %r on %d, ready %d, %d "
                           "drawn" % st)

        # --- 5: Esc while it fills cancels the play, nothing drawn
        prefilled("the prefill held")
        q.hmp("sendkey esc")
        wait(lambda: rb("vp_played") == 1 and rb("vp_sess") == 0,
             "Esc to cancel", 30)
        print("   Esc: %s" % state())
        if rw("vp_done") != 0 or rb("vp_err"):
            bad.append("Esc in the prefill drew %d (error %d)"
                       % (rw("vp_done"), rb("vp_err")))

        # --- 6: the prefill whole, B: blank, and Space plays from the bank
        prefilled("the prefill held again")
        q.hmp("stop")
        lc, cnt = rw("vp_lc"), rw("vp_xcnt")
        q.hmp("change floppy1 %s raw" % blank)
        q.hmp("cont")
        # the reader started at the cluster under frame 0's super-packet:
        # chunk c is file bytes from there, so what ring and bank hold ends
        # at least at (lc + cnt) chunks past the first super-packet less a
        # cluster
        first = min(k[3] for k in rd88.keys if k[3])
        lo = first - 4096
        reach = lo + (lc + cnt) * CHUNK
        ring = lo + lc * CHUNK
        fits = [k for k in rd88.keys
                if k[3] and HOLD0 < k[0] and k[3] + k[4] * 512 <= reach]
        print("   swapped at lc=%d cnt=%d: the ring reaches %d, ring and "
              "bank %d; key frames inside: %s"
              % (lc, cnt, ring, reach, [k[0] for k in fits]))
        if not fits or fits[-1][3] <= ring:
            bad.append("no key frame lies past the ring and inside the bank "
                       "- the swap leg proves nothing")
        else:
            n = fits[-1][0]
            qpoke(q, [(base + syms["vp_stopat"], struct.pack("<H", n))])
            q.hmp("sendkey spc")
            wait(lambda: rb("vp_ready") == 1, "Space to start the play", 30)
            hold_at(n, "past the swap", 120)
            check(n)
            # --- 7: what neither held is the blank disk's
            release(0xFFFF)
            ok = os88qemu.acted(q, lambda: rb("vp_ready") == 0, secs=60,
                                what="the play to stop", poll=0.05)
            print("   after the bank: %s" % state())
            if ok and rw("vp_done") == NF and not rb("vp_err"):
                bad.append("with B: blank the play STILL reached the end - "
                           "the swap does not bite")
            if not ok:
                q.hmp("sendkey esc")
        q.close()
    if bad:
        print("\nvidbank: FAIL")
        for b in bad:
            print("  - " + b)
        return 1
    print("\nvidbank: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
