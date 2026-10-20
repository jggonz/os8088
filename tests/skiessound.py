#!/usr/bin/env python3
"""AN ENGINE EACH (SPEC.md 88.8.2), on MartyPC.

    python3 tests/skiessound.py [--machine os8088_5150_herc_gla]

Every aeroplane used to make the same noise - `[cs_thr] + 50`, so a Fouga
Magister and an Icon A5 were the same note at the same lever. Each one reads
its own engine record now, and this asks the guest what it is actually
playing rather than what the table says it should:

  1. every powered aeroplane's tone is its OWN record's law at three lever
     positions - idle, half and full - read off `[cs_tone]` on the machine
     and computed on the host from the record the guest holds;
  2. the four of them are FOUR DIFFERENT NOTES at full power, which is the
     whole of the ask and the one check a shared record cannot pass;
  3. a SHUT THROTTLE IS AN IDLE and not silence, which is the change that
     does most of the work: an engine that is running is never silent;
  4. the WASSMER BIJAVE plays nothing at any lever position - it has no
     engine record, and 88.7.6.1 gave it that silence long before this;
  5. the note is STEADY - ONE value over sixteen settled ticks. A beat that
     dropped it on a share of the ticks was heard in the field as "a periodic
     dip that does sound like a bug, rather than an engine", with its rate
     moving as the frame rate moved, and this is the check that replaced it;
  6. the FOUGA'S THRUST LAGS THE HAND. Its record sets CSSF_SPOOL, so it
     follows the thrust the engine HAS (88.7.5) and not the lever: the note
     has not arrived even 24 ticks after its own slew settled, where a
     piston is at full note the moment the slew is done;
  7. the note GLIDES. A throttle that shuts in one step - the brake does
     exactly that (88.7.10) - used to change the note in one tick, which the
     field heard as "playing a note" rather than an engine. CSS_LAG makes it
     fall through the range: ten distinct notes on the Cessna, thirty-five on
     the Magister, and it still ARRIVES.

Three red runs (docs/WRITING-TESTS.md 1). --clobber-lag puts CSS_LAG to 0 on
every engine so the note snaps to each new value, and check 7 goes red on both
aeroplanes with "0 distinct notes". --clobber-shared points every plane
at the Cessna's record: check 2 goes red because five aeroplanes are then one
note, check 6 because a jet reading the lever arrives at once, and the
record-versus-thrust rule because the sailplane is holding an engine. Check 1
stays GREEN in that arm and is meant to - it asks whether each aeroplane plays
the record it HOLDS, and under the clobber they all honestly do, which is
exactly why check 2 has to exist separately. --clobber-spool clears the
Magister's CSSF_SPOOL so it reads the lever like everything else, and 6 goes
red on its own while every other check stays green: the jet still has its own
NUMBERS and still glides, it has just stopped lagging the hand, which is the
mechanic it is here for.
"""
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88ui                                               # noqa: E402
import dispapps                                             # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FAILED = []


def _equates():
    """The record offsets, READ OUT of skies.asm - skiesfleet.py's rule."""
    out = {}
    for line in open(os.path.join(ROOT, "apps", "skies", "skies.asm")):
        m = re.match(r"^(CS[A-Z]*_[A-Z0-9_]+)\s+equ\s+"
                     r"(-?(?:0[xX][0-9A-Fa-f]+|\d+))\s*(?:;|$)", line)
        if m:
            out[m.group(1)] = int(m.group(2), 0)
    return out


E = _equates()
_WANT = "CSP_SND CSP_THRUST CSS_IDLE CSS_SPAN CSS_LAG CSS_CAP CSS_FLAGS " \
        "CSSF_SPOOL".split()
_missing = [k for k in _WANT if k not in E]
if _missing:
    sys.exit("skiessound: skies.asm no longer declares %s" % ", ".join(_missing))


def check(cond, what):
    print("%s  %s" % ("ok  " if cond else "FAIL", what))
    if not cond:
        FAILED.append(what)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--clobber-shared", action="store_true",
                    help="give every aeroplane the Cessna's engine: red")
    ap.add_argument("--clobber-spool", action="store_true",
                    help="make the Magister read the lever: red")
    ap.add_argument("--clobber-lag", action="store_true",
                    help="CSS_LAG 0 on every engine, so the note snaps: red")
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

        def poke(n, d):
            m.write(lin + base + off(n), d)

        def rec(at, o):
            return int.from_bytes(m.readseg(seg, at + o, 2), "little")

        def recb(at, o):
            return m.readseg(seg, at + o, 1)[0]

        def name(at):
            return m.readseg(seg, rec(at, 0), 24).split(b"\0")[0].decode()

        def dropbox():
            """The Plane drop-down's box, WAITED FOR rather than read once.

            `cs_drplane`'s clip is filled in when the title page arms the
            control, and reading it the instant the package is launched
            answers (0, 168, 335, 183) - a box whose centre is 167, which is
            41 pixels left of the real one at 208. Every click then misses
            and the row that is picked is the row that was already picked,
            which reads exactly like a drop-down that does not work.
            """
            for _ in range(20):
                p = [rec(mp["cs_drplane"], 2 * i) for i in range(4)]
                if p[0]:
                    return p
                m.advance(frames=20)
                m.run()
            sys.exit("skiessound: the Plane list never armed its clip")

        def inbracket():
            """SPEC.md 88.10.5's own predicate, and it is a PAIR.

            `[cs_back]` is the mode the bracket took and is never cleared on
            the way out, so it answers "has this program ever flown", not
            "is it flying". `[cs_quit]` is what the F key sets and what the
            loop leaves on.
            """
            return byte("cs_back") != 0 and byte("cs_quit") == 0

        if a.clobber_shared:
            # every aeroplane gets the Cessna's engine, the sailplane too
            src = rec(mp["cs_planes"], 0)
            snd = rec(src, E["CSP_SND"])
            for i in range(5):
                p = rec(mp["cs_planes"], 2 * i)
                m.write(lin + p + E["CSP_SND"], snd.to_bytes(2, "little"))
        if a.clobber_lag:
            for i in range(5):
                p = rec(mp["cs_planes"], 2 * i)
                sn = rec(p, E["CSP_SND"])
                if sn:                          # both terms: a shift of 0 on
                    m.write(lin + sn + E["CSS_LAG"], b"\x00\x00")
        if a.clobber_spool:
            jet = rec(mp["cs_planes"], 4)
            s = rec(jet, E["CSP_SND"])
            m.write(lin + s + E["CSS_FLAGS"], b"\x00")

        def leave():
            """Out of the bracket and back to the title page, CONFIRMED.

            `F TOGGLES` (88.10.5), so this asks ONCE and then WAITS: a
            second `f` typed while the first is still being acted on walks
            straight back into the bracket. What it waits ON is `inbracket()`
            and NOT `[cs_back]` alone - 88.10.5 is explicit that cs_back is
            the mode the bracket took and is NEVER CLEARED on the way out, so
            a loop polling it for zero waits for ever. A frame count would
            not do either: a mode restore is not a fixed number of frames,
            and a crash (88.7) holds the picture for CS_CRASHT ticks with the
            key going nowhere.
            """
            if not inbracket():
                return
            m.type_text("f")                    # ONCE: F TOGGLES
            for _ in range(20):
                m.advance(frames=40)
                m.run()
                if not inbracket():
                    m.advance(frames=60)        # ...and let the mode restore
                    m.run()                     # finish before the next click
                    return
            sys.exit("skiessound: the bracket would not close")

        def pick(row):
            """Leave the bracket and select row `row`; out: its record."""
            leave()
            want = rec(mp["cs_planes"], 2 * row)
            for _ in range(3):
                po = dropbox()
                ui.mo.click((po[0] + po[2]) // 2, (po[1] + po[3]) // 2)
                m.advance(frames=25)
                m.run()
                # THE OPEN LIST'S OWN TOP, read back out of the control
                # (tests/skiesadi.py's spelling), not derived from the closed
                # box's bottom edge: a computed one picks the wrong row.
                top = rec(mp["cs_drplane"], 22)
                ui.mo.click(po[0] + 20, top + 1 + 12 * row + 6)
                m.advance(frames=25)
                m.run()
                if w("cs_plane") == want:
                    break
            got = w("cs_plane")
            if got != want:
                sys.exit("skiessound: row %d picked %04x, wanted %04x"
                         % (row, got, want))
            return got

        def enter(row):
            """...and ENTER, polled for leave()'s reason one direction along.

            `f` is typed ONCE because it toggles, and what follows is a wait
            rather than a frame count: cs_cmd_fly reads the picked location's
            world off the floppy (88.10.5), so the first flight into a world
            is a disk transfer and not a repaint.
            """
            m.type_text("f")
            for _ in range(20):
                m.advance(frames=40)
                m.run()
                if inbracket():
                    return
            sys.exit("skiessound: row %d took no mode" % row)

        def fly(row):
            """Pick row `row`, enter the bracket; out: its plane record."""
            got = pick(row)
            enter(row)
            return got

        def ticks(n, pin):
            """Run n sim ticks, calling pin() at the top of each."""
            for _ in range(n):
                pin()
                m.run()
                if m.wait_stop(30) is None:
                    sys.exit("skiessound: cs_sound_step.tick never ran")

        def hold(plane, thr, spool=True):
            """A pin that keeps the aeroplane flying at throttle `thr`.

            The aeroplane is held in the AIR and the lever re-pinned on every
            tick, so nothing the ground model does to the throttle (the brake
            closes it, 88.7.10) can reach the reading. SPOOL is pinned with it
            unless the caller is measuring the lag itself.
            """
            # THE MODEL'S OWN TARGET, and not a proportion of full thrust.
            # cs_step computes it as (thr x CSP_THRUST / 100) in WHOLE units
            # before shifting into 8.8, so 50% of a 19-unit engine is 9 and
            # not 9.5 - pin the proportion instead and cs_step drags it back
            # every tick, the thrust never settles, and the note never settles
            # either. Pin what the model wants and the gap is zero.
            want = ((thr * rec(plane, E["CSP_THRUST"]) // 100) << 8) & 0xFFFF

            def pin():
                poke("cs_py", (600 * 256).to_bytes(4, "little"))
                poke("cs_thr", thr.to_bytes(2, "little"))
                if spool:
                    poke("cs_thracc", want.to_bytes(2, "little"))
            return pin

        def bp_on():
            m.bp_exec(lin + mp["cs_sound_step.tick"])
            m.run()
            if m.wait_stop(30) is None:
                sys.exit("skiessound: cs_sound_step.tick never ran")

        def bp_off():
            m.bp_exec()
            m.run()

        def settle(plane, thr, spool=True, cap=150):
            """Tick until the NOTE STOPS MOVING, and say how long it took.

            CSS_LAG means the note closes on what the engine wants rather than
            arriving there (88.8.2.1), so a reading taken the tick after the
            lever moved is a reading of the glide. Convergence is asked for
            rather than counted out: a trainer's 45 Hz takes about fifteen
            ticks at a shift of 2 and the jet's 520 takes four times that at 3,
            and a fixed count would be either wrong or slow.
            """
            pin = hold(plane, thr, spool)
            last, same, n = None, 0, 0
            while n < cap:
                pin()
                v = w("cs_eng")
                same = same + 1 if v == last else 0
                last = v
                if same >= 3:
                    return n
                m.run()
                if m.wait_stop(30) is None:
                    sys.exit("skiessound: cs_sound_step.tick never ran")
                n += 1
            return n

        def tones(plane, thr, nn=8, spool=True):
            """(note, thrust) over nn ticks at throttle `thr`, once SETTLED."""
            settle(plane, thr, spool)
            pin, out = hold(plane, thr, spool), []
            for _ in range(nn):
                out.append((w("cs_tone"), w("cs_thracc")))
                pin()
                m.run()
                if m.wait_stop(30) is None:
                    sys.exit("skiessound: cs_sound_step.tick never ran")
            return out

        def law(snd, plane, thr, thracc):
            """What 88.8.2 says the note is, off the GUEST's own record.

            Hz = CSS_IDLE + source x CSS_SPAN / full scale, and the SOURCE is
            the lever unless CSSF_SPOOL says it is the thrust the engine has -
            scaled off cs_thracc's 8.8 DIRECTLY, at the resolution the sound
            actually uses.
            """
            span = rec(snd, E["CSS_SPAN"])
            if recb(snd, E["CSS_FLAGS"]) & E["CSSF_SPOOL"]:
                full = rec(plane, E["CSP_THRUST"]) << 8
                add = thracc * span // full
            else:
                add = thr * span // 100
            return rec(snd, E["CSS_IDLE"]) + add

        # --- 1, 3, 5: each aeroplane's own law, its idle, and its beat ------
        full_notes = {}
        for row in range(5):
            plane = fly(row)
            nm = name(plane)
            snd = rec(plane, E["CSP_SND"])

            # AN AEROPLANE HAS AN ENGINE RECORD IF AND ONLY IF IT HAS THRUST,
            # which is the whole rule and the one the fleet has to keep as it
            # grows. It is checked per row rather than as a count so a future
            # sixth aeroplane is covered by arriving.
            check((snd != 0) == (rec(plane, E["CSP_THRUST"]) != 0),
                  "%s: CSP_SND is %s and CSP_THRUST is %d - an aeroplane has "
                  "an engine record exactly when it has an engine"
                  % (nm, "set" if snd else "0",
                     rec(plane, E["CSP_THRUST"])))

            bp_on()
            if snd == 0:                        # --- 4: the sailplane -------
                quiet = all(t == 0 for thr in (0, 50, 100)
                            for t, _ in tones(plane, thr, nn=4))
                bp_off()
                check(quiet, "%s has no engine record and plays NOTHING at "
                             "any lever position (88.7.6.1)" % nm)
                continue

            for thr in (0, 50, 100):
                seq = tones(plane, thr)
                want = law(snd, plane, thr, seq[-1][1])
                bad = [t for t, ta in seq if t != law(snd, plane, thr, ta)]
                check(not bad,
                      "%s at throttle %3d: %d Hz, which is its record's law"
                      % (nm, thr, want))
                if thr == 100:
                    full_notes[nm] = want
                if thr == 0:
                    check(want > 0, "%s IDLES at %d Hz rather than falling "
                                    "silent (88.8.2)" % (nm, want))
            # 5: THE NOTE IS STEADY. It is the check the field asked for in as
            # many words - a beat that dropped the note on a share of the ticks
            # read as "a periodic dip that does sound like a bug, rather than
            # an engine", and its RATE moved with the frame rate. One value
            # over sixteen settled ticks is what replaced it.
            got = {t for t, _ in tones(plane, 100, nn=16)}
            bp_off()
            check(len(got) == 1,
                  "%s holds ONE note over 16 settled ticks (%s Hz), with no "
                  "beat in it (88.8.2.1)" % (nm, sorted(got)))

        # --- 2: four engines, four notes ------------------------------------
        check(len(set(full_notes.values())) == len(full_notes),
              "the four powered aeroplanes are four DIFFERENT notes at full "
              "power: %s" % ", ".join("%s %d Hz" % (k, v)
                                      for k, v in sorted(full_notes.items(),
                                                         key=lambda x: x[1])))

        # --- 6: the jet's note is off the THRUST and not the lever ----------
        # CSP_SPOOL is 5, so cs_thracc closes on a target cs_step computes in
        # WHOLE units before shifting into 8.8: 50% of a 19-unit engine is 9
        # and not 9.5. That rounding is what makes the two sources SEPARABLE
        # at a half-open lever - 426 Hz off the thrust against 440 off the
        # lever - and separable is what a check needs.
        #
        # IT USED TO BE A TIMING CHECK and could not stay one. "Still climbing
        # 24 ticks after its own slew settled" discriminated while the note
        # arrived quickly; CSS_CAP's ceiling (88.8.2.1) makes the Magister's
        # own glide take a hundred ticks, which is longer than the spool, so
        # the spool stopped being what the delay measured. A check that has
        # stopped being about its subject passes for the wrong reason.
        plane = fly(2)
        nm = name(plane)
        snd = rec(plane, E["CSP_SND"])
        bp_on()
        settle(plane, 50)
        got = w("cs_eng")
        bp_off()
        # BOTH LAWS COMPUTED HERE and neither through law(), which honours
        # the guest's CSS_FLAGS - under --clobber-spool that would quietly
        # move the "thrust" figure to the lever's and the red run would read
        # as two identical numbers disagreeing.
        full = rec(plane, E["CSP_THRUST"])
        idle, span = rec(snd, E["CSS_IDLE"]), rec(snd, E["CSS_SPAN"])
        thrust = idle + (((50 * full // 100) << 8) * span) // (full << 8)
        lever = idle + 50 * span // 100
        check(got == thrust and got != lever,
              "%s at a half-open lever plays %d Hz - the thrust it HAS "
              "(%d), not what the lever asks for (%d): 88.7.5's spool, and "
              "at cs_thracc's own 8.8" % (nm, got, thrust, lever))

        # --- 7: THE NOTE GLIDES, which is what the field asked for ----------
        # "The stepping, between throttle levels, sounds more like it is
        # playing a note than switching engine pitches." A throttle that shuts
        # in one step - the brake does exactly that (88.7.10) - used to change
        # the note in one tick. CSS_LAG makes it fall through the range
        # instead, so the check is that the drop takes TIME and passes through
        # the middle of it rather than jumping the gap.
        for row in (0, 2):
            plane = fly(row)
            nm = name(plane)
            snd = rec(plane, E["CSP_SND"])
            lag = recb(snd, E["CSS_LAG"])
            cap = recb(snd, E["CSS_CAP"])
            bp_on()
            settle(plane, 100)
            hi = w("cs_eng")
            # ...and now SHUT it, in one step, and watch the note come down
            # ...and run until it STOPS, not for a fixed count. How long a
            # descent takes is CSS_CAP's business now - a constant interval
            # per tick, so the time is proportional to the OCTAVES crossed,
            # and the Magister's 1.8 are four times the trainer's 0.8.
            lo = law(snd, plane, 0, 0)
            seq, pin = [], hold(plane, 0)
            for _ in range(200):
                pin()
                seq.append(w("cs_eng"))
                if len(seq) > 3 and seq[-1] == seq[-2] == seq[-3]:
                    break
                m.run()
                if m.wait_stop(30) is None:
                    sys.exit("skiessound: cs_sound_step.tick never ran")
            bp_off()
            mid = [v for v in seq if lo < v < hi]
            check(len(set(mid)) >= 4 and seq[1] > lo,
                  "%s (CSS_LAG %d, CSS_CAP %d) GLIDES %d -> %d Hz through %d "
                  "distinct notes over %d ticks rather than changing in one "
                  "(88.8.2.1)" % (nm, lag, cap, hi, lo, len(set(mid)),
                                  len(seq)))
            check(seq[-1] <= lo + 1,
                  "%s still ARRIVES: %d Hz against an idle of %d, %d ticks "
                  "after the lever shut" % (nm, seq[-1], lo, len(seq)))

        # --- 8: NO RAMP ON ENTRY --------------------------------------------
        # [cs_eng] starts at 0 and used to GLIDE up to idle, so every flight
        # opened with a rising note the aeroplane never makes - the field's
        # "on entry to the scene they all start at one point, and change to
        # another point. They should probably all start at their idle point
        # without ramping to it." A note of 0 is the sentinel for "not
        # running yet" and the first tick SNAPS to whatever the engine wants.
        #
        # It has to be watched from OUTSIDE the bracket: by the time fly()
        # has confirmed the mode, a ramp would be long over. The breakpoint
        # goes on before the F, so the first stop IS the flight's first tick.
        for row in (0, 2):
            plane = pick(row)
            nm = name(plane)
            snd = rec(plane, E["CSP_SND"])
            m.bp_exec(lin + mp["cs_sound_step.tick"])
            enter(row)
            if m.wait_stop(30) is None:
                sys.exit("skiessound: cs_sound_step.tick never ran on entry")
            seq = []
            for _ in range(6):
                m.run()
                if m.wait_stop(30) is None:
                    sys.exit("skiessound: cs_sound_step.tick never ran")
                seq.append((w("cs_eng"), w("cs_thr"), w("cs_thracc")))
            bp_off()
            want = [law(snd, plane, t, ta) for _, t, ta in seq]
            got = [v for v, _, _ in seq]
            check(got == want,
                  "%s is AT its note from the first tick of the flight (%s Hz "
                  "against %s) - no ramp up to idle (88.8.2.1)"
                  % (nm, got, want))

        leave()

    print("skiessound: %d check(s) failed" % len(FAILED) if FAILED
          else "skiessound: all checks passed")
    return 1 if FAILED else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
