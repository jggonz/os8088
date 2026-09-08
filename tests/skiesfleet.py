#!/usr/bin/env python3
"""THE THREE NEW AEROPLANES (SPEC.md 88.7.5-88.7.7), on MartyPC.

    python3 tests/skiesfleet.py [--machine os8088_5150_herc_gla]

Each one exists for a MECHANIC and not for a number on the airspeed
indicator, so each check is about the mechanic:

  1. the Plane list has five rows and every row is the record it names;
  2. FOUGA MAGISTER - cs_att_lag. Held hard over, the roll rate RAMPS: the
     first tick moves a fraction of what the eighth does, where the two
     direct-drive aeroplanes move the same amount every tick. Released, the
     rate DECAYS instead of stopping dead - and the tail is SHORT, so a tap
     held for two ticks is a 2.2-degree nudge that has stopped moving well
     inside 26 (88.7.5.1: it was 19.6 degrees and still going). And the
     engine SPOOLS: thrust climbs toward the throttle over seconds, which no
     piston does;
  3. WASSMER BIJAVE - no engine. It starts in the AIR at CSP_LAUNCH with the
     tow-released message, its thrust is zero however hard the throttle key
     is held, and left alone it comes down;
  4. ICON A5 - CSPF_AMPHIB. It starts ON THE WATER, gets off it under its own
     power, and a touchdown inside the water strip is a LANDING with the
     water's name on the strip. The same touchdown in the Cessna is a crash,
     which is what ditching is.

Three red runs (docs/WRITING-TESTS.md 1). --clobber-lag gives the Magister
the trainer's CSP_ATT, and the ramp and decay checks go red. --clobber-tail
makes the rate decay as slowly as it builds, which is the model as it was,
and the tap becomes 6.45 degrees and never settles. --clobber-amphib
clears the A5's CSP_FLAGS, and the water start and the splash go red - note
that everything ELSE about the A5 still passes, which is why those two
checks are the ones that are there.
"""
import argparse
import math
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88ui                                               # noqa: E402
import dispapps                                             # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEG = 65536.0 / 360.0
# THE RECORD LAYOUTS ARE READ OUT OF skies.asm, not copied into here. There
# were six of these as literals and 88.7.7.1 wanted fourteen more - a walk of
# the guest's own object table needs the model and object offsets too - and
# twenty hand-copied numbers is twenty chances to go quietly stale. equ lines
# are not in the map dispapps builds, so they are parsed the way
# tests/unit/t_csworld.py parses them.
def _equates():
    out = {}
    for line in open(os.path.join(ROOT, "apps", "skies", "skies.asm")):
        m = re.match(r"^(CS[A-Z]*_[A-Z0-9_]+)\s+equ\s+"
                     r"(-?(?:0[xX][0-9A-Fa-f]+|\d+))\s*(?:;|$)", line)
        if m:
            out[m.group(1)] = int(m.group(2), 0)      # base 0: CSO_POI is hex
    return out


_E = _equates()
_WANT = ("CSP_VSTALL CSP_VROT CSP_VMAX CSP_THRUST CSP_ROLLR CSP_COCKPIT CSP_ATT "
         "CSP_SPOOL CSP_LAUNCH CSP_FLAGS CSA_X CSA_Z CSA_WX CSA_WZ CSA_WHDG "
         "CSA_WLEN CSA_WWID CSA_OBJS CSA_NOBJ CSO_SIZE CSO_MODEL CSO_X CSO_Z "
         "CSO_NAME CSM_TYPE CSM_NF CSM_VERTS CSM_FACES CSM_FLAT CSI_RIVER "
         "CS_MSGAGE").split()
_miss = [n for n in _WANT if n not in _E]
if _miss:
    sys.exit("skiesfleet: skies.asm no longer defines %s" % ", ".join(_miss))
globals().update({n: _E[n] for n in _WANT})
CSG_TAKEOFF, CSG_RELEASE, CSG_SPLASH, CSG_TOAST = 1, 7, 8, 9
# ...and csflight.inc's own, which is where the air lives
_EF = {}
for _l in open(os.path.join(ROOT, "apps", "skies", "csflight.inc")):
    _m = re.match(r"^(CS[A-Z]*_[A-Z0-9_]+)\s+equ\s+(-?\d+)\s*(?:;|$)", _l)
    if _m:
        _EF[_m.group(1)] = int(_m.group(2))
for _n in ("CS_NLIFT", "CSAIR_SIZE", "CSAIR_DX", "CSAIR_DZ", "CSAIR_RATE",
           "CS_AIRTILE", "CS_LIFTCLR"):
    if _n not in _EF:
        sys.exit("skiesfleet: csflight.inc no longer defines %s" % _n)
    globals()[_n] = _EF[_n]
CS_ST_GROUND, CS_ST_AIR, CS_ST_CRASH = 0, 1, 2
bad = []


def check(cond, what):
    print("  [%s] %s" % ("PASS" if cond else "FAIL", what))
    if not cond:
        bad.append(what)


def sg(v):
    return v - 0x10000 if v >= 0x8000 else v


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--clobber-lag", action="store_true",
                    help="give the Magister the trainer's model: must go red")
    ap.add_argument("--clobber-amphib", action="store_true",
                    help="take the A5's amphibious flag away: must go red")
    ap.add_argument("--clobber-water", action="store_true",
                    help="cs_inwater always answers NO: must go red")
    ap.add_argument("--clobber-tail", action="store_true",
                    help="make the rate decay as slowly as it builds: red")
    a = ap.parse_args(argv)
    os.chdir(ROOT)
    mp = dispapps._map("skies")

    def off(n):
        return dispapps.bss_off("skies", n)

    with os88ui.boot(a.image, apps=a.apps, machine=a.machine) as ui:
        m = ui.m
        ui.path("B:/GAMES/SKIES.O88")
        slot, seg = dispapps.pkg_seg(m, 0)
        lin = seg << 4
        base = int.from_bytes(m.readseg(seg, 8, 2), "little")

        def w(n):
            return int.from_bytes(m.readseg(seg, base + off(n), 2), "little")

        def byte(n):
            return m.readseg(seg, base + off(n), 1)[0]

        def dw(n):
            return int.from_bytes(m.readseg(seg, base + off(n), 4), "little")

        def poke(n, d):
            m.write(lin + base + off(n), d)

        def rec(at, o):
            return int.from_bytes(m.readseg(seg, at + o, 2), "little")

        def name(at):
            p = rec(at, 0)
            return m.readseg(seg, p, 24).split(b"\0")[0].decode()

        m.advance(frames=30)
        m.run()
        if a.clobber_lag:
            m.pause()
            m.write(lin + mp["cs_p_fouga"] + CSP_ATT,
                    mp["cs_att_trim"].to_bytes(2, "little"))
            m.run()
            print("  (the Magister given the trainer's model: this must fail)")
        if a.clobber_tail:
            # cs_lagax turns a quarter of the gap into three quarters on the
            # decay arm with `neg ax / add ax, cx`; without those the rate
            # dies as slowly as it builds, which is the model as it was
            lo, hi = mp["cs_lagax"], mp["cs_move"]
            code = m.read(lin + lo, hi - lo)
            i = code.find(b"\xF7\xD8\x01\xC8")      # neg ax ; add ax, cx
            if i < 0:
                sys.exit("skiesfleet: cs_lagax does not shape its decay the "
                         "way this patch expects")
            m.pause()
            m.write(lin + lo + i, b"\x90\x90\x90\x90")
            m.run()
            print("  (the rate made to decay as slowly as it builds: must fail)")
        if a.clobber_water:
            # cs_inwater blanked to `clc / ret` (SPEC.md 88.7.7.1). BOTH water
            # landings go red, which is the point: since the strip stopped
            # being what a touchdown is tested against, this routine is the
            # whole of what makes a splashdown a landing
            m.pause()
            m.write(lin + mp["cs_inwater"], b"\xF8\xC3")
            m.run()
            print("  (cs_inwater made to answer NO: this must fail)")
        if a.clobber_amphib:
            m.pause()
            m.write(lin + mp["cs_p_a5"] + CSP_FLAGS, b"\x00\x00")
            m.run()
            print("  (the A5's amphibious flag cleared: this must fail)")

        # --- 1. the list ------------------------------------------------------
        n = (mp["cs_plnames"] - mp["cs_planes"]) // 2
        check(n == 5, "the Plane list has five rows (%d)" % n)
        po = [rec(mp["cs_drplane"], 2 * i) for i in range(4)]

        def prompt():
            b = m.readseg(seg, base + off("cs_promptb"), 44)
            return b.split(b"\x00")[0].decode("latin-1")

        def fly(row):
            """Pick row `row` and enter the bracket; out: the plane record."""
            if byte("cs_back") != 0:
                m.type_text("f")
                m.advance(frames=60)
                m.run()
            ui.mo.click((po[0] + po[2]) // 2, (po[1] + po[3]) // 2)
            m.advance(frames=25)
            m.run()
            ui.mo.click(po[0] + 20, po[3] + 2 + 12 * row + 6)
            m.advance(frames=25)
            m.run()
            got, want = w("cs_plane"), rec(mp["cs_planes"], 2 * row)
            check(got == want, "row %d picks the record it names (%04x)" % (row, got))
            m.type_text("f")
            m.advance(frames=100)
            m.run()
            check(byte("cs_back") != 0, "row %d: the bracket took a mode" % row)
            # THE PROMPT NAMES THIS AEROPLANE'S OWN SPEED (88.7.9). It was a
            # literal 55, which is the Cessna's rotate speed in knots, on the
            # panel of a jet that leaves the ground at 81 - and the knots are
            # cs_k_spd's own conversion, so the sentence cannot come to
            # disagree with the needle it is telling you to watch
            if rec(got, CSP_LAUNCH):        # ...or, on an aeroplane the tow
                vs = rec(got, CSP_VSTALL)   # left flying, 1.5 VSTALL
                v = vs + 2 * (vs >> 2)
            else:
                v = rec(got, CSP_VROT)
            kt = (v * 996) >> 16
            txt = prompt()
            check(txt.endswith(" %d KNOTS" % kt),
                  "row %d's prompt names ITS OWN speed, %d knots (%r)"
                  % (row, kt, txt))
            return got

        def ticks(nn, pin=None):
            """Stop at the top of each of nn consecutive cs_step calls."""
            m.bp_exec(lin + mp["cs_step"])
            m.run()
            if m.wait_stop(30) is None:
                sys.exit("skiesfleet: cs_step never ran")
            if pin:
                pin()
            out = []
            for _ in range(nn):
                out.append(None)
                m.run()
                if m.wait_stop(30) is None:
                    sys.exit("skiesfleet: cs_step never ran")
                out[-1] = True
            m.bp_exec()
            m.run()

        def sample(nn, what, pin=None):
            m.bp_exec(lin + mp["cs_step"])
            m.run()
            assert m.wait_stop(30) is not None
            if pin:
                pin()
            out = []
            for _ in range(nn):
                out.append(what())
                m.run()
                assert m.wait_stop(30) is not None
            m.bp_exec()
            m.run()
            return out

        def airborne(spd, alt=600, pitch=0):
            m.pause()
            for nm, v in (("cs_px", -2400), ("cs_py", alt), ("cs_pz", -2000)):
                poke(nm, ((v * 256) & 0xFFFFFFFF).to_bytes(4, "little"))
            poke("cs_hdg", (7282).to_bytes(2, "little"))
            poke("cs_pitch", (int(pitch * DEG) & 0xFFFF).to_bytes(2, "little"))
            poke("cs_roll", b"\x00\x00")
            poke("cs_rrate", b"\x00\x00")
            poke("cs_prate", b"\x00\x00")
            poke("cs_spd", (spd * 128).to_bytes(2, "little"))
            poke("cs_state", b"\x01")

        # --- 2. the Magister --------------------------------------------------
        jet = fly(2)
        print("    --- %s: SPOOL %d, ROLLR %d"
              % (name(jet), rec(jet, CSP_SPOOL), rec(jet, CSP_ROLLR)))
        m.key("ArrowRight", down=True, up=False)
        m.advance(frames=8)
        m.run()
        rolls = sample(9, lambda: sg(w("cs_roll")), lambda: airborne(120))
        m.key("ArrowRight", down=False, up=True)
        steps = [rolls[i + 1] - rolls[i] for i in range(len(rolls) - 1)]
        print("      roll steps held: %s" % steps)
        # THE FIRST STEP IS DROPPED and that is not a fudge: the attitude is
        # pinned at a cs_step breakpoint, which is inside a frame whose
        # cs_input has already run, so the first tick after it moves nothing
        # whatever model is fitted - a leading zero that made this check pass
        # against the trainer's model too, which is the false green
        # --clobber-lag exists to catch.
        steps = steps[1:]
        check(steps[0] * 2 < steps[-1],
              "the roll rate RAMPS - the second tick is under half the "
              "eighth (%d against %d)" % (steps[0], steps[-1]))
        m.advance(frames=10)
        m.run()
        after = sample(5, lambda: sg(w("cs_roll")))
        dec = [after[i + 1] - after[i] for i in range(len(after) - 1)]
        print("      roll steps released: %s" % dec)
        check(dec[0] > 0 and dec[0] > dec[-1],
              "...and DECAYS when the stick is centred instead of stopping "
              "dead (%s)" % dec)

        # --- 2b. the tail is SHORT, and a TAP is a nudge (SPEC.md 88.7.5.1)
        # Both of the owner's complaints about this aeroplane are one number.
        # A rate decaying by a quarter a tick has THREE TIMES its current
        # value still to travel, so centring the stick at the horizon coasted
        # a fifth of a turn past it and a one-tick tap rolled 19.6 degrees.
        # The decay is three quarters a tick now and the tail is a third of
        # the rate.
        def stickticks(key, held, n=26):
            # Hold `key` through `held` + 1 TICKS, which only cs_stick's own
            # breakpoint makes expressible: the stick is read per tick now
            # (88.7.5.1) and a frame is three of them. EVERY HIT OF THIS
            # BREAKPOINT IS A SIM TICK since 88.7.5.2 - cs_input used to call
            # cs_stick as well, and while it did, one stop in three or four
            # was that call and moved nothing.
            m.pause()
            airborne(120)
            poke("cs_roll", b"\x00\x00")
            m.run()
            m.bp_exec(lin + mp["cs_stick"])
            m.run()
            assert m.wait_stop(30) is not None
            m.key(key, down=True, up=False)
            out = []
            for i in range(n):
                out.append(sg(w("cs_roll")))
                if i == held:
                    m.key(key, down=False, up=True)
                m.run()
                assert m.wait_stop(30) is not None
            m.bp_exec()
            m.run()
            m.key(key, down=False, up=True)
            return out

        tap = stickticks("ArrowRight", 1)       # TWO ticks - a release sent
                                                # at the same halt as the press
                                                # never reaches the guest
        moved = abs(tap[-1]) / DEG
        print("      a short tap: %.2f degrees, settled %s"
              % (moved, "yes" if tap[-1] == tap[-4] else "no"))
        check(0.5 < moved < 6.0,
              "a TAP is a nudge and not a manoeuvre (%.2f degrees)" % moved)
        check(tap[-1] == tap[-4],
              "...and it has stopped moving well inside %d ticks (%d then %d)"
              % (len(tap), tap[-4], tap[-1]))
        # the spool
        m.pause()
        airborne(120)
        poke("cs_thr", (100).to_bytes(2, "little"))
        poke("cs_thrust", b"\x00\x00")
        poke("cs_thracc", b"\x00\x00")
        m.run()
        thr = sample(40, lambda: w("cs_thrust"))
        full = rec(jet, CSP_THRUST)
        print("      thrust over 40 ticks: %d -> %d (full %d)"
              % (thr[0], thr[-1], full))
        check(thr[0] < full and thr[-1] > thr[0],
              "the engine SPOOLS toward the throttle rather than arriving "
              "(%d -> %d of %d)" % (thr[0], thr[-1], full))
        check(thr[-1] < full,
              "...and forty ticks is not enough to get there (%d of %d)"
              % (thr[-1], full))

        # --- 3. the Bijave ----------------------------------------------------
        gl = fly(3)
        launch = rec(gl, CSP_LAUNCH)
        alt = int.from_bytes(m.readseg(seg, base + off("cs_py") + 1, 2), "little")
        print("    --- %s: LAUNCH %d, THRUST %d, started at %d m, msg %d"
              % (name(gl), launch, rec(gl, CSP_THRUST), alt, byte("cs_msg")))
        check(byte("cs_state") == CS_ST_AIR and abs(alt - launch) <= 2,
              "the sailplane starts in the AIR at CSP_LAUNCH (state %d, %d m)"
              % (byte("cs_state"), alt))
        check(byte("cs_msg") == CSG_RELEASE,
              "...and says the tow is released (%d)" % byte("cs_msg"))
        m.key("KeyW", down=True, up=False)         # the throttle, held wide open
        m.advance(frames=30)
        m.run()
        m.key("KeyW", down=False, up=True)
        check(w("cs_thrust") == 0,
              "no throttle key can give it thrust (%d)" % w("cs_thrust"))
        m.pause()
        airborne(25, alt=900, pitch=-2)
        m.run()
        a0 = int.from_bytes(m.readseg(seg, base + off("cs_py") + 1, 2), "little")
        x0, z0 = sg(dw("cs_px") >> 8) & 0xFFFF, 0
        x0 = sg(int.from_bytes(m.readseg(seg, base + off("cs_px") + 1, 2), "little"))
        z0 = sg(int.from_bytes(m.readseg(seg, base + off("cs_pz") + 1, 2), "little"))
        ticks(120)
        a1 = int.from_bytes(m.readseg(seg, base + off("cs_py") + 1, 2), "little")
        x1 = sg(int.from_bytes(m.readseg(seg, base + off("cs_px") + 1, 2), "little"))
        z1 = sg(int.from_bytes(m.readseg(seg, base + off("cs_pz") + 1, 2), "little"))
        drop = a0 - a1
        run = int(((x1 - x0) ** 2 + (z1 - z0) ** 2) ** 0.5)
        print("      two degrees down: %d m of height for %d m of ground"
              % (drop, run))
        check(drop > 0, "it comes DOWN with no engine (%d m)" % drop)
        check(run > drop * 12,
              "...and it GLIDES rather than falling - better than 12:1 "
              "(%d:%d)" % (run, drop))

        # --- 3b. the glider's three, from the field (88.7.6.1-88.7.6.3) -------
        # W ON AN ENGINELESS AEROPLANE. It used to open the throttle to 100
        # like everyone else's, and cs_sound_step makes the ENGINE TONE out
        # of exactly that word - so the Bijave hummed (88.7.6.1)
        m.key("KeyW", down=True, up=False)
        for _ in range(8):
            m.advance(frames=25)
            m.run()
        m.key("KeyW", down=False, up=True)
        m.advance(frames=20)
        m.run()
        check(w("cs_thr") == 0 and w("cs_tone") == 0,
              "W opens no throttle on a glider and makes no engine tone "
              "(thr %d, tone %d)" % (w("cs_thr"), w("cs_tone")))

        # THE PROMPT EXPIRES (88.7.6.2). A sailplane starts in the AIR, so the
        # liftoff that clears CSG_TAKEOFF never happens and the tow release
        # had no clearer at all - it stood until something else spoke
        m.pause()
        poke("cs_msg", bytes([CSG_RELEASE]))
        poke("cs_msgt", bytes([CS_MSGAGE]))
        m.run()
        gone = False
        for _ in range(40):
            m.advance(frames=25)
            m.run()
            if byte("cs_msg") == 0:
                gone = True
                break
        check(gone, "the tow release goes by itself (msg %d, %d ticks left)"
                    % (byte("cs_msg"), byte("cs_msgt")))

        # ...AND IT GOES UNDER A TOAST TOO (88.13.8 borrows the strip). The
        # announcement is banked in cs_toastwas while the toast is up, so an
        # ager that cleared cs_msg would cut the toast short - and one that
        # left cs_toastwas alone would put the expired release straight back
        # on the glass when the toast went, which is the same complaint by a
        # second route. Raised by hand rather than by a setting key, because
        # what is under test is the AGER and not the settings page.
        m.pause()
        poke("cs_msg", bytes([CSG_RELEASE]))
        poke("cs_msgt", bytes([CS_MSGAGE]))
        m.run()
        m.advance(frames=25)                        # ...let it start counting
        m.pause()
        poke("cs_toastwas", bytes([CSG_RELEASE]))   # the toast takes the strip
        poke("cs_msg", bytes([CSG_TOAST]))
        poke("cs_toastt", bytes([200]))             # ...and holds it far past
        m.run()                                     # the announcement's own age
        held = True
        for _ in range(40):
            m.advance(frames=25)
            m.run()
            if byte("cs_msg") != CSG_TOAST:
                held = False
                break
            if byte("cs_msgt") == 0 and byte("cs_toastwas") == 0:
                break
        check(held and byte("cs_toastwas") == 0 and byte("cs_msg") == CSG_TOAST,
              "an announcement that ages out UNDER a toast retires where it "
              "is kept (msg %d, was %d, toast %d ticks left)"
              % (byte("cs_msg"), byte("cs_toastwas"), byte("cs_toastt")))
        m.pause()                                   # ...and put the strip back
        poke("cs_toastt", b"\x00")
        poke("cs_msg", b"\x00")
        m.run()

        # THE AIR (88.7.6.3, 88.7.6.4): still, lift, sink - read off cs_airv,
        # and the ALTITUDE with it, because a rate nothing moves is not
        # weather. The table's offsets are inside a TILE that repeats, so a
        # rect is reached at its own place in ANY tile - which is the property
        # under test and not an accident of the arithmetic: the rows below
        # step one whole tile out on each axis and expect the same air
        port = w("cs_airport")
        fx, fz = sg(rec(port, CSA_X)), sg(rec(port, CSA_Z))
        lifts = []
        for row in range(CS_NLIFT):
            at = mp["cs_lifts"] + row * CSAIR_SIZE
            lifts.append((sg(rec(at, CSAIR_DX)), sg(rec(at, CSAIR_DZ)),
                          sg(rec(at, CSAIR_RATE))))
        up = max(lifts, key=lambda r: r[2])
        dn = min(lifts, key=lambda r: r[2])

        def soar(dx, dz):
            """Drop the glider in at 900 m, wings level, and see what the air
            does with it."""
            # THE PAUSE GOES ON FIRST AND A FRAME IS LET BY (docs/WRITING
            # -TESTS.md 13 row 22): m.pause() lands anywhere, and a cs_step
            # in flight finishes on resume and writes its own position over
            # the pin - which lands the aeroplane somewhere that is not the
            # rect, so no crossing happens and the swoop reads 0.
            m.pause()
            poke("cs_pause", b"\x01")
            m.run()
            m.advance(frames=2)
            m.pause()
            poke("cs_px", (((fx + dx) * 256) & 0xFFFFFFFF).to_bytes(4, "little"))
            poke("cs_pz", (((fz + dz) * 256) & 0xFFFFFFFF).to_bytes(4, "little"))
            poke("cs_py", ((900 * 256) & 0xFFFFFFFF).to_bytes(4, "little"))
            poke("cs_pitch", b"\x00\x00")
            poke("cs_roll", b"\x00\x00")
            poke("cs_spd", (22 * 128).to_bytes(2, "little"))
            poke("cs_state", b"\x01")
            poke("cs_pause", b"\x00")          # ...and only NOW does it fly
            m.run()
            m.advance(frames=4)
            m.run()
            sw = byte("cs_swt")                 # the CROSSING's own swoop
            m.advance(frames=26)
            m.run()
            y0 = sg(int.from_bytes(m.readseg(seg, base + off("cs_py") + 1, 2),
                                   "little"))
            air = sg(w("cs_airv"))
            m.advance(frames=60)
            m.run()
            y1 = sg(int.from_bytes(m.readseg(seg, base + off("cs_py") + 1, 2),
                                   "little"))
            return air, y0, y1, sw

        calm = soar(0, 400)                     # INSIDE the calm bubble
        lift = soar(up[0], up[1])
        sink = soar(dn[0], dn[1])
        far = soar(up[0] + CS_AIRTILE, up[1] + CS_AIRTILE)   # the NEXT tile
        print("      the air: calm %+d (%d->%d), lift %+d (%d->%d, swoop %d), "
              "sink %+d (%d->%d, swoop %d), one tile on %+d"
              % (calm[0], calm[1], calm[2], lift[0], lift[1], lift[2], lift[3],
                 sink[0], sink[1], sink[2], sink[3], far[0]))
        check(calm[0] == 0 and calm[1] == calm[2],
              "the CALM BUBBLE round the field is still (%+d, %d -> %d)"
              % (calm[0], calm[1], calm[2]))
        check(lift[0] == up[2] and lift[2] > lift[1],
              "the strongest lift is the table's and it CLIMBS (%+d for %+d, "
              "%d -> %d)" % (lift[0], up[2], lift[1], lift[2]))
        check(sink[0] == dn[2] and sink[2] < sink[1],
              "...and the deepest sink SINKS (%+d for %+d, %d -> %d)"
              % (sink[0], dn[2], sink[1], sink[2]))
        check(lift[3] > 0 and sink[3] > 0,
              "and CROSSING into either arms the swoop (%d, %d)"
              % (lift[3], sink[3]))
        # ...AND IT TILES (88.7.6.4), which is the whole of the fix for air
        # nobody could find: the same rect one tile out on BOTH axes
        check(far[0] == up[2] and far[2] > far[1],
              "one whole tile out on both axes the same lift is there (%+d "
              "for %+d, %d -> %d)" % (far[0], up[2], far[1], far[2]))

        # --- 4. the A5 --------------------------------------------------------
        a5 = fly(4)
        port = w("cs_airport")
        wlen = rec(port, CSA_WLEN)
        print("    --- %s: FLAGS %04x, the location's water strip is %d m long"
              % (name(a5), rec(a5, CSP_FLAGS), 2 * wlen))
        check(byte("cs_onwater") == 1,
              "the amphibian starts ON THE WATER (%d)" % byte("cs_onwater"))
        m.key("KeyW", down=True, up=False)         # THE THROTTLE HELD TO 100
        for _ in range(12):                        # and not merely nudged: the
            m.advance(frames=25)                   # key moves it 2 a tick, and
            m.run()                                # a third of the way open is
            if w("cs_thr") >= 100:                 # less thrust than a hull's
                break                              # drag, so it would sit there
        m.key("KeyW", down=False, up=True)
        check(w("cs_thr") >= 100, "the throttle opens (%d%%)" % w("cs_thr"))
        m.key("ArrowDown", down=True, up=False)
        ok = False
        for _ in range(60):
            m.advance(frames=40)
            m.run()
            if byte("cs_state") == CS_ST_AIR:
                ok = True
                break
            if byte("cs_state") == CS_ST_CRASH:
                break
        m.key("ArrowDown", down=False, up=True)
        check(ok, "...and gets off it under its own power (state %d, %d kt)"
              % (byte("cs_state"), w("cs_spd") * 1944 // 128000))

        # ...AND THE WATER STOPS IT (88.7.7.2), which twice the rolling
        # friction did not: measured over the same span with the throttle
        # shut against the same span with it open, because the hull only
        # brakes when the pilot is not driving - a hull that dragged harder
        # than the engine pushes is the check above going red
        def hull(thr):
            m.pause()
            poke("cs_pause", b"\x01")
            m.run(); m.advance(frames=2); m.pause()
            poke("cs_spd", (22 * 128).to_bytes(2, "little"))
            poke("cs_thr", thr.to_bytes(2, "little"))
            poke("cs_thrust", b"\x00\x00")
            poke("cs_thracc", b"\x00\x00")
            poke("cs_state", b"\x00")
            poke("cs_onwater", b"\x01")
            poke("cs_pause", b"\x00")
            m.run()
            s0 = w("cs_spd")
            for _ in range(10):
                m.advance(frames=12)
                m.run()
            return s0, w("cs_spd")
        shut = hull(0)
        open_ = hull(100)
        print("      on the water: throttle shut %d -> %d, open %d -> %d"
              % (shut[0], shut[1], open_[0], open_[1]))
        check(shut[0] - shut[1] > 3 * (open_[0] - open_[1]),
              "the WATER stops it with the throttle shut, and does not with "
              "it open (%d units against %d)"
              % (shut[0] - shut[1], open_[0] - open_[1]))

        # --- 4b. the brake is a LATCH the panel shows (88.7.10.1) -----------
        # A TYPED key and not a held one: the field could not tell a held B
        # from no B at all, and a level read leaves nothing on the glass.
        m.pause()
        poke("cs_pause", b"\x01")
        m.run(); m.advance(frames=2); m.pause()
        poke("cs_onwater", b"\x00")
        poke("cs_spd", (30 * 128).to_bytes(2, "little"))
        poke("cs_thr", (100).to_bytes(2, "little"))
        poke("cs_state", b"\x00")
        poke("cs_pause", b"\x00")
        m.run()
        m.type_text("b")
        latched = False
        for _ in range(30):
            m.advance(frames=10)
            m.run()
            if byte("cs_kbrake"):
                latched = True
                break
        check(latched, "one TYPED b latches the brake (%d)" % byte("cs_kbrake"))
        thr_after = w("cs_thr")
        held = byte("cs_kbrake")
        for _ in range(20):                     # ...and it STAYS, unheld
            m.advance(frames=12)
            m.run()
            held = held and byte("cs_kbrake")
        check(held and thr_after == 0,
              "...and it stays on with nothing held, throttle shut (%d, thr "
              "%d)" % (byte("cs_kbrake"), thr_after))
        m.type_text("b")                        # ...and the same key lets go
        released = False
        for _ in range(30):
            m.advance(frames=10)
            m.run()
            if byte("cs_kbrake") == 0:
                released = True
                break
        check(released,
              "...and the same key lets it go (%d)" % byte("cs_kbrake"))

        def waters():
            """Every CSI_RIVER face of the flown location, out of the GUEST's
            own object table (SPEC.md 88.7.7.1) - so this row does not carry a
            coordinate anybody has to keep in step with the world files.

            Returns (centroid x, centroid z, the object's name) per face."""
            objs, nobj = rec(port, CSA_OBJS), rec(port, CSA_NOBJ)
            out = []
            for o in range(objs, objs + nobj * CSO_SIZE, CSO_SIZE):
                md = rec(o, CSO_MODEL)
                if m.readseg(seg, md + CSM_TYPE, 1)[0] != CSM_FLAT:
                    continue
                ox, oz = sg(rec(o, CSO_X)), sg(rec(o, CSO_Z))
                vt, f = rec(md, CSM_VERTS), rec(md, CSM_FACES)
                for _ in range(m.readseg(seg, md + CSM_NF, 1)[0]):
                    hdr = m.readseg(seg, f, 3)
                    idx = m.readseg(seg, f + 3, hdr[0])
                    if hdr[1] == CSI_RIVER:
                        pts = [(sg(rec(vt, 4 * i)), sg(rec(vt, 4 * i + 2)))
                               for i in idx]
                        out.append((ox + sum(p[0] for p in pts) // len(pts),
                                    oz + sum(p[1] for p in pts) // len(pts),
                                    name(o + CSO_NAME)))
                    f += 3 + hdr[0]
            return out

        def onstrip(px, pz):
            """...is that point inside the old rectangle? (88.7.7.1)"""
            wx, wz = sg(rec(port, CSA_WX)), sg(rec(port, CSA_WZ))
            th = sg(rec(port, CSA_WHDG)) * 2 * math.pi / 65536.0
            dx, dz = px - wx, pz - wz
            return (abs(dx * math.sin(th) + dz * math.cos(th))
                    <= sg(rec(port, CSA_WLEN))
                    and abs(dx * math.cos(th) - dz * math.sin(th))
                    <= sg(rec(port, CSA_WWID)))

        def splashdown(at=None):
            """Put the aeroplane a metre over the water strip, sinking gently
            along it, and let the touchdown happen. `at` puts it somewhere
            else instead, which is what 88.7.7.1 is about."""
            m.pause()
            sn = (rec(port, CSA_WX), rec(port, CSA_WZ)) if at is None else at
            poke("cs_px", ((sg(sn[0]) * 256) & 0xFFFFFFFF).to_bytes(4, "little"))
            poke("cs_pz", ((sg(sn[1]) * 256) & 0xFFFFFFFF).to_bytes(4, "little"))
            poke("cs_py", ((3 * 256) & 0xFFFFFFFF).to_bytes(4, "little"))
            poke("cs_hdg", rec(port, CSA_WHDG).to_bytes(2, "little"))
            # TWO DEGREES DOWN and not a poked vertical speed: cs_step
            # recomputes cs_vs from the attitude every tick, so a poked one
            # is gone before the next touchdown test and the aeroplane hangs
            # there at three metres for ever
            poke("cs_pitch", (int(-2 * DEG) & 0xFFFF).to_bytes(2, "little"))
            poke("cs_roll", b"\x00\x00")
            poke("cs_spd", (25 * 128).to_bytes(2, "little"))
            poke("cs_state", b"\x01")
            m.run()

        land0 = w("cs_landings")
        splashdown()
        for _ in range(20):
            m.advance(frames=20)
            m.run()
            if byte("cs_state") != CS_ST_AIR:
                break
        print("      after the touchdown: state %d, water %d, msg %d, "
              "landings %d -> %d" % (byte("cs_state"), byte("cs_onwater"),
                                     byte("cs_msg"), land0, w("cs_landings")))
        check(byte("cs_state") == CS_ST_GROUND and byte("cs_onwater") == 1,
              "a touchdown on the water strip is a LANDING (state %d, water %d)"
              % (byte("cs_state"), byte("cs_onwater")))
        check(byte("cs_msg") == CSG_SPLASH,
              "...and the strip names the water (msg %d)" % byte("cs_msg"))
        check(w("cs_landings") == land0 + 1,
              "...and it counts (%d -> %d)" % (land0, w("cs_landings")))

        # --- ALL WATER IS WATER (SPEC.md 88.7.7.1) ---------------------------
        # The strip was an invisible runway on the river: 800 m of the Seine
        # were landable and the rest of it was a crash. Take the water face
        # FURTHEST from the strip that is not inside it, and put the same
        # touchdown there - it is a landing now, and it names ITS OWN water.
        far = [(px, pz, nm) for px, pz, nm in waters() if not onstrip(px, pz)]
        check(bool(far), "the flown world has water off the strip to test (%d "
                         "faces)" % len(far))
        if far:
            wx, wz = sg(rec(port, CSA_WX)), sg(rec(port, CSA_WZ))
            px, pz, nm = max(far, key=lambda r: abs(r[0] - wx) + abs(r[1] - wz))
            print("      off-strip water: %r at (%d, %d), %d m from the strip"
                  % (nm, px, pz, abs(px - wx) + abs(pz - wz)))
            land1 = w("cs_landings")
            splashdown((px & 0xFFFF, pz & 0xFFFF))
            for _ in range(20):
                m.advance(frames=20)
                m.run()
                if byte("cs_state") != CS_ST_AIR:
                    break
            check(byte("cs_state") == CS_ST_GROUND
                  and byte("cs_onwater") == 1,
                  "water OFF the strip is a landing too (state %d, water %d)"
                  % (byte("cs_state"), byte("cs_onwater")))
            check(byte("cs_msg") == CSG_SPLASH
                  and w("cs_landings") == land1 + 1,
                  "...it counts and it splashes (msg %d, %d -> %d)"
                  % (byte("cs_msg"), land1, w("cs_landings")))

        # ...and the edge is still an edge: dry land is a crash
        dry = [(px, pz) for px, pz, _ in waters()]
        ox = max(abs(p[0]) for p in dry) + 3000
        splashdown((ox & 0xFFFF, 0))
        for _ in range(20):
            m.advance(frames=20)
            m.run()
            if byte("cs_state") != CS_ST_AIR:
                break
        check(byte("cs_state") == CS_ST_CRASH,
              "...and dry land off the runway is still a crash (state %d)"
              % byte("cs_state"))

        # ...and the same touchdown in the trainer is a ditching
        fly(0)
        splashdown()
        for _ in range(20):
            m.advance(frames=20)
            m.run()
            if byte("cs_state") != CS_ST_AIR:
                break
        check(byte("cs_state") == CS_ST_CRASH,
              "the SAME touchdown in the Cessna is a crash (state %d)"
              % byte("cs_state"))
        m.type_text("f")
        m.advance(frames=40)
        m.run()

    if bad:
        for b in bad:
            print("FAIL: " + b)
        return 1
    print("  ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
