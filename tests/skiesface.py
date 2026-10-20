#!/usr/bin/env python3
"""THE CENTRELINE'S STRIPES ARE AHEAD OF THE AEROPLANE (SPEC.md 88.6.2.4).

    python3 tests/skiesface.py [--clobber-face]

cs_rwline walked the stripes one way only - from the aeroplane's own u
toward the far end - which is right for a take-off roll from the near
threshold and exactly backwards after a landing from the far side, where
everything it drew was BEHIND the aeroplane. The field reported it as a
blank runway.

THE ORACLE IS THE ARGUMENT AND NOT THE PICTURE. Drawing the frame twice,
once with a `ret` poked over cs_rwline, and counting the differing pixels
does not repeat here: the same build at the same pose gave 18, 816 and
2,038, because m.advance counts EMULATOR frames and a forced repaint lands
a different number of guest frames each time. What repeats exactly is what
cs_rwsegu is HANDED - mapped back into the model's own u with the guest's
own [cs_rwrev], then judged against the end the aeroplane is REALLY
pointed at, which this file sets rather than reads, so a broken build
cannot pass by agreeing with itself.

The long solid run is exempt: 88.6.2.3 puts the runway behind you in one
segment and the stripes at the threshold ahead.

--clobber-face is the red run (docs/WRITING-TESTS.md 1): it NOPs the five
bytes that set [cs_rwrev], the walk is one-way again, and seven of the
sixteen poses put the line behind the aeroplane.
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
import os88ui, dispapps
CLOB = "--clobber-face" in sys.argv
CSA_X, CSA_Z, CSA_ELEV, CSA_HDG, CSA_HLEN = 2, 4, 6, 8, 10
def sg(v): return v - 0x10000 if v >= 0x8000 else v
mp = dispapps._map("skies")
with os88ui.boot("build/os8088-360.img", apps="build/apps360.img",
                 machine="os8088_5150_herc_gla") as ui:
    m = ui.m
    ui.path("B:/GAMES/SKIES.O88")
    slot, seg = dispapps.pkg_seg(m, 0)
    lin = seg << 4
    base = int.from_bytes(m.readseg(seg, 8, 2), "little")
    def off(n): return dispapps.bss_off("skies", n)
    def uw(n): return int.from_bytes(m.readseg(seg, base + off(n), 2), "little")
    def poke(n, d): m.write(lin + base + off(n), d)
    def rec(at, o): return int.from_bytes(m.readseg(seg, at + o, 2), "little")
    m.advance(frames=30); m.run()
    if CLOB:
        # `mov byte [cs_rwrev], 1` - the whole of 88.6.2.4. Without it the
        # walk is one-way again, which is the code the field photographed.
        lo = mp["cs_rwline"]
        code = m.read(lin + lo, 0x80)
        j = code.find(b"\xC6\x06")
        while j >= 0 and code[j+4] != 1:
            j = code.find(b"\xC6\x06", j + 1)
        if j < 0: sys.exit("rwseg: no `mov byte [cs_rwrev], 1`")
        m.pause(); m.write(lin + lo + j, b"\x90" * 5); m.run()
        print("  (the direction test is gone: one-way again)")
    m.type_text("f"); m.advance(frames=120); m.run()
    ap_ = uw("cs_airport")
    ax_, az = sg(rec(ap_, CSA_X)), sg(rec(ap_, CSA_Z))
    elev, hdg = sg(rec(ap_, CSA_ELEV)), rec(ap_, CSA_HDG)
    hlen = sg(rec(ap_, CSA_HLEN))
    rws, rwc = sg(uw("cs_rwsin")), sg(uw("cs_rwcos"))
    m.pause(); poke("cs_pause", b"\x01"); m.run(); m.advance(frames=2); m.pause()
    print("  %-28s %6s  %s" % ("pose", "own u", "what cs_rwsegu is handed"))
    bad = 0
    for t, y in ((400,2),(200,2),(0,2),(-200,2),(-400,2),
                 (200,30),(200,80),(200,140)):
        for back in (1, 0):
            px = ax_ + (t*rws)//32768
            pz = az  + (t*rwc)//32768
            for nm, v in (("cs_px",px),("cs_py",elev+y),("cs_pz",pz)):
                m.write(lin+base+off(nm), ((v*256)&0xFFFFFFFF).to_bytes(4,"little"))
            poke("cs_hdg", ((hdg + (0x8000 if back else 0)) & 0xFFFF).to_bytes(2,"little"))
            poke("cs_pitch", b"\x00\x00"); poke("cs_roll", b"\x00\x00")
            poke("cs_state", b"\x00" if y <= 3 else b"\x01")
            # TWO WHOLE GUEST FRAMES, not eight emulator ones: at ~5 fps
            # against 60, `advance(frames=8)` is under one guest frame, so
            # the capture below could still be reading the PREVIOUS pose's
            # segments. That is what made this read 0 of 16 and then 2 of 16
            # on the same build.
            m.run()
            f0 = int.from_bytes(m.readseg(seg, base + off("cs_frames"), 2),
                                "little")
            for _ in range(80):
                m.advance(frames=4)
                if ((int.from_bytes(m.readseg(seg, base + off("cs_frames"), 2),
                                    "little") - f0) & 0xFFFF) >= 2:
                    break
            m.pause()
            m.bp_exec(lin + mp["cs_rwsegu"]); m.run()
            segs = []
            for _ in range(6):
                if m.wait_stop(8) is None: break
                r = m.regs(); segs.append((r["ax"], r["cx"])); m.run()
            m.pause(); m.bp_exec()
            # THE GUEST'S OWN along, not the one I asked for: the position
            # poke goes through sin/cos and lands a few metres off, which is
            # most of a stripe pitch and read as a failure.
            alng = sg(uw("cs_along"))
            ownu = ((alng + hlen) * 32768) // (2 * hlen)
            rev = m.readseg(seg, base + off("cs_rwrev"), 1)[0]
            # THE BREAKPOINT IS AT cs_rwsegu's FIRST BYTE, so AX/CX are still
            # FACING-SPACE - the mirror is the first thing inside. So own u
            # has to be mirrored too, which the first version of this oracle
            # did not do and reported nine good poses as failures.
            ownf = (32767 - ownu) if rev else ownu
            pitch = 2 * uw("cs_rwdu")
            rwfar = uw("cs_rwfar")
            # Back into MODEL u with the guest's own rev, then judged against
            # the direction the aeroplane is REALLY pointed (which the test
            # sets, not the guest) - so the red arm cannot pass by agreeing
            # with itself. The long SOLID run is exempt either way: it is the
            # runway behind you by design (88.6.2.3).
            mseg = [((32767 - b, 32767 - a) if rev else (a, b)) for a, b in segs]
            keep = [(a, b) for a, b in mseg if (b - a) <= 4 * pitch]
            if ownf >= rwfar:
                # 88.6.2.3's run is ANCHORED ON THE THRESHOLD and not on the
                # aeroplane, so its first stripe legitimately starts a little
                # behind one that is already inside the run. That case is
                # tests/skiesrwy.py's and not this one's.
                ok, note = True, "  (88.6.2.3's anchored run)"
            elif back:          # facing the NEAR end: ahead is DECREASING u
                ok, note = bool(keep) and all(b <= ownu + pitch
                                              for a, b in keep), ""
            else:
                ok, note = bool(keep) and all(a >= ownu - pitch
                                              for a, b in keep), ""
            if not ok:
                bad += 1
            print("  %-30s %6d %6d rev%d  %s%s"
                  % ("along %+5d, %3d up, face %s" % (alng, y, "NEAR" if back else "FAR"),
                     ownu, ownf, rev, mseg[:3],
                     note if ok else "   <-- BEHIND the aeroplane"))
            m.run()
    print("  %d of %d poses draw the line BEHIND the aeroplane" % (bad, 16))
    if bad:
        print("FAIL: the centreline is drawn behind the aeroplane at %d of 16 "
              "poses" % bad)
        sys.exit(1)
    print("  ok")
