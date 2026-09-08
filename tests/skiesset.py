#!/usr/bin/env python3
"""CLEAR SKIES' SETTINGS (SPEC.md 88.13): the page, and the four knobs on it
reaching the picture, on MartyPC.

    python3 tests/skiesset.py [--machine os8088_5150_herc_gla]

Every setting trades picture for frame rate, so every check here is about
one of the two: either the renderer does less work, or the glass shows
something different.

  0. the Detail Level a player who never opens the page flies on is
     MODERATE (88.13.1.1) - in the byte and in the drop-down's own record -
     because Moderate carries every location's whole table and High is the
     286/386 rung above it;
  1. Flight -> Settings opens the page and the painter writes all seven
     controls' rects (four drop-downs, two fill boxes, Done);
  2. picking Detail Level = Low leaves fewer objects in the frame - cs_nvisn,
     which is what the cull filed - and the frame gets measurably shorter;
  3. a fill box toggles its bit in cs_setfill, and clearing both leaves the
     wireframe: the ground's dither is gone from the glass;
  3a. the rungs NEST and Moderate is High minus CSO_DENSE, COUNTED and not
     compared as pixels: a paused Clear Skies is not a still picture - the
     water moves - so two arms drawing the same objects differ by thousands
     of pixels and a SAME-RUNG control reads the same thousands. Taken with
     the dense bits ON and with them CLEARED IN THE GUEST'S OWN TABLE: the
     EQUAL branch is what a broken ladder trips and the STRICT branch is what
     says the flag reaches the cull at all, and neither alone is enough -
     --clobber-dense drops collidables at Low and Moderate together, so with
     the bits on the counts still nest and still differ. The equal branch
     used to be provided by a location with no CSO_DENSE and since 88.13.1.3
     there is none, so it is synthesised rather than left to stop running;
  3c. and a CSO_DENSE building is NOT SOLID below High (88.13.1.4) - the
     aeroplane is put inside one at Moderate and must fly on, and inside the
     same one at High and must crash into it by name. cs_collide reads the
     table and never the ladder, so without the gate Manhattan's twelve kill
     a player at the DEFAULT rung out of clear air;
  3b. Detail Level = None files nothing built and still leaves the
     runway, the water and the terrain standing (88.13.1), and Only
     Roads brings the roads and bridges back and no more;
  4. inside the bracket the hotkeys CYCLE, one key a setting (88.13.5): F1
     the detail level, F2 the draw distance, F3 the size, F4/F5 the two
     fills - each stepping its own ladder one rung and round at the top,
     walked a FULL LAP so the wrap is seen and not only the step;
  4c. ...and every one of them raises a TOAST naming the setting and its new
     value (88.13.8), checked against the SETTINGS PAGE'S OWN list of names
     read out of the guest rather than a copy typed here, and then left to
     expire back to the strip it replaced. A fill's toast says FILL or WIRE
     and not on or off, because a cleared bit is a wireframe and not a thing
     gone; and a SECOND toast has to REPAINT the strip, which is the panel's
     key and not the byte - two toasts were the same cs_msg and the same
     cs_crashwhy, so the second setting changed under a line still naming
     the first;
  6. and the page's answer SURVIVES A CLOSE (88.13.9): four settings picked,
     the page left by Done, the window closed, the package opened again -
     and the file in SYSTEM\APPDATA is what it comes back with;
  4b. the page's controls behave: a drop-down's list actually COMES DOWN
     (banked and on the glass, not merely marked open), and Done is drawn
     down on the press, cancels on a release off it and turns the page only
     on a release over it (88.13.6);
  5. and shrinking the view CLEARS THE PIXELS BESIDE IT. cs_clearall zeroes
     the shadow and the blit copies only the view's byte columns out of it,
     so without cs_scrclear the larger view's ground stands in a band either
     side of the smaller picture (88.13.4). That band is read out of VRAM
     and not out of the rendered frame - see the note at the check.

Three red runs (docs/WRITING-TESTS.md 1). --clobber-clear NOPs the call to
cs_scrclear, which is that band exactly, and check 5 must go red.
--clobber-drwin takes the page's drop-downs' OS88UI_DR_WIN away, which is
the defect exactly, and the bank and glass checks must go red - note that
the PICK still works without it, which is why those two checks exist.
--clobber-arm puts a ret on os88ui_arm so Done never arms, and the release
must then fail to turn the page. --clobber-default puts the old top-rung
default back and check 0 must go red; --clobber-dense points cs_consider's
`test ax, CSO_DENSE` at CSO_COLLIDE, which objects actually wear, so the top
rung's filter fires at Moderate too and check 3a must go red.
--clobber-solid makes cs_collide's `jz .solid` unconditional, which is that
walker exactly as it was before 88.13.1.4 - every collidable solid at every
rung, so a High tier kills a player at the default one - and check 3c must go
red.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88ui                                               # noqa: E402
import os88mouse                                            # noqa: E402
import dispapps                                             # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CPS = 4772727
CSBL_NONE, CSBL_ROADS, CSBL_LOW, CSBL_MOD, CSBL_HIGH = 0, 1, 2, 3, 4
CSL_NEAR, CSL_MOD, CSL_FAR, CSL_ULTRA = 0, 1, 2, 3
CSZ_SMALL, CSZ_MOD, CSZ_FULL = 0, 1, 2
CSA_OBJS, CSA_NOBJ = 18, 20        # the location's object table
CSO_SKIP, CSO_SIZE = 18, 20        # ...and one object's deferral
CSFL_TERRAIN, CSFL_BLDG, CSFL_ALL = 1, 2, 3
CSG_TAKEOFF, CSG_TOAST = 1, 9
CSM_STACK = 0                           # a model of LEVELS, so its first pair
                                        # of words IS a footprint - a CSM_FLAT
                                        # model's are its first vertex
bad = []


def check(cond, what):
    print("  [%s] %s" % ("PASS" if cond else "FAIL", what))
    if not cond:
        bad.append(what)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--clobber-clear", action="store_true",
                    help="NOP the screen clear a size change owes: must go red")
    ap.add_argument("--clobber-drwin", action="store_true",
                    help="take the page's drop-downs' window handle away")
    ap.add_argument("--clobber-default", action="store_true",
                    help="the old CSBL_HIGH default back: check 0 goes red")
    ap.add_argument("--clobber-dense", action="store_true",
                    help="the top rung's filter fires at Moderate too: check"
                         " 3a goes red")
    ap.add_argument("--clobber-solid", action="store_true",
                    help="cs_collide stops asking the rung, so a CSO_DENSE"
                         " building is solid at Moderate: check 3c goes red")
    ap.add_argument("--clobber-arm", action="store_true",
                    help="put a ret on os88ui_arm, so Done never arms")
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

        def rec(name, o):
            return int.from_bytes(m.readseg(seg, mp[name] + o, 2), "little")

        def rect(name):
            return [rec(name, 2 * i) for i in range(4)]

        def to_rung(key, name, want, rungs):
            """A hotkey CYCLES since 88.13.5, so a rung is reached by stepping
            to it. Up to two laps, because a press the guest has not read yet
            is a press this must not count twice - cs_input polls int 16h once
            a FRAME and a frame here can be 400 ms."""
            for _ in range(2 * rungs + 2):
                m.pause()
                v = byte(name)
                m.run()
                if v == want:
                    return True
                m.key(key)
                m.advance(frames=40)
                m.run()
            return False

        def bld(rung):
            return to_rung("F1", "cs_setbld", rung, CSBL_HIGH + 1)

        m.advance(frames=30)
        m.run()
        if a.clobber_solid:
            # the `jz .solid` that skips 88.13.1.4's rung test when the object
            # is not dense, made unconditional: every collidable is solid at
            # every rung again, which is cs_collide exactly as it was
            lo, hi = mp["cs_collide"], mp["cs_crash"]
            code = m.read(lin + lo, hi - lo)
            i = code.find(b"\xA9\x00\x02\x74")     # test ax, CSO_DENSE / jz
            if i < 0:
                sys.exit("skiesset: cs_collide does not hold 88.13.1.4's rung "
                         "test where this patch expects it")
            m.pause()
            m.write(lin + lo + i + 3, b"\xEB")
            m.run()
            print("  (cs_collide no longer asks the rung: this run must fail)")
        if a.clobber_clear:
            lo, hi = mp["cs_hotkey"], mp["cs_hotkey"] + 0x120
            code = m.read(lin + lo, hi - lo)
            site = None
            for i in range(len(code) - 3):
                if code[i] == 0xE8:
                    r = int.from_bytes(code[i + 1:i + 3], "little")
                    r = r - 0x10000 if r >= 0x8000 else r
                    if lo + i + 3 + r == mp["cs_scrclear"]:
                        site = lo + i
                        break
            if site is None:
                sys.exit("skiesset: cs_hotkey does not call cs_scrclear")
            m.pause()
            m.write(lin + site, b"\x90\x90\x90")
            m.run()
            print("  (the size change's screen clear NOPed: this run must fail)")
        if a.clobber_drwin:
            m.pause()
            for i in range(4):
                at = int.from_bytes(m.readseg(seg, mp["cs_setdrops"] + 2 * i, 2),
                                    "little")
                m.write(lin + at + 14, b"\x00\x00")     # OS88UI_DR_WIN
            m.run()
            print("  (the page's drop-downs given no window: this run must fail)")
        if a.clobber_arm:
            m.pause()
            m.write(lin + mp["os88ui_arm"], b"\xC3")
            m.run()
            print("  (os88ui_arm is a ret: this run must fail)")

        if a.clobber_default:
            m.pause()
            m.write(lin + base + off("cs_setbld"), bytes([CSBL_HIGH]))
            m.write(lin + mp["cs_drbld"] + 12, CSBL_HIGH.to_bytes(2, "little"))
            m.run()
            print("  (the old top-rung default back: this run must fail)")
        if a.clobber_dense:
            # `test ax, CSO_DENSE` is A9 00 02. Point it at CSO_COLLIDE, which
            # objects actually wear, and the top rung's filter starts firing at
            # Moderate - the defect check 3a exists for.
            lo, hi = mp["cs_consider"], mp["cs_range"]
            code = m.read(lin + lo, hi - lo)
            i = code.find(b"\xA9\x00\x02")
            if i < 0:
                sys.exit("skiesset: cs_consider does not test CSO_DENSE the "
                         "way this patch expects - re-read it before trusting "
                         "the red run")
            m.pause()
            m.write(lin + lo + i + 1, b"\x01\x00")
            m.run()
            print("  (the dense filter pointed at CSO_COLLIDE: must fail)")

        # --- 0. THE DEFAULT IS MODERATE (SPEC.md 88.13.1) --------------------
        #
        # Moderate carries every location's whole table today, so this is the
        # picture the simulator has always drawn; High is the rung that adds
        # CSO_DENSE on top of it and is a 286/386 one. Read BEFORE the page is
        # opened, because opening it is what would set the byte if the record
        # and the init disagreed.
        check(byte("cs_setbld") == CSBL_MOD,
              "the Detail Level a player who never opens the page flies on is "
              "Moderate (%d)" % byte("cs_setbld"))
        drdef = int.from_bytes(m.readseg(seg, mp["cs_drbld"] + 12, 2), "little")
        check(drdef == CSBL_MOD,
              "...and the page's own drop-down agrees (%d)" % drdef)

        # --- 1. the page and its controls ------------------------------------
        ui.menu_pick("Flight", "Settings")
        m.advance(frames=40)
        m.run()
        check(byte("cs_page") == 2, "Flight -> Settings turns to the page (%d)"
              % byte("cs_page"))
        # MODE IS NOT ONE OF THEM ON THIS MACHINE (SPEC.md 88.13.11). This
        # row flies a Hercules, which has one raster, so the Mode row is left
        # OFF the page rather than greyed and the painter writes SIX rects.
        # Asserting seven made this row red for the fix working. The row that
        # covers Mode on both kinds of display is `skiesmode`; the predicate
        # is not repeated here, because a second copy of it in Python is a
        # second thing to get wrong.
        names = ("cs_drbld", "cs_drlod", "cs_drsize",
                 "cs_ckterr", "cs_ckbld", "cs_donerect")
        rects = {n: rect(n) for n in names}
        wrote = [n for n in names if rects[n][2] > rects[n][0]]
        check(len(wrote) == len(names),
              "the painter wrote all six live controls' rects (%d)" % len(wrote))
        r = rect("cs_drmode")
        check(r[2] <= r[0],
              "...and the MODE row, which this display cannot use, has no "
              "rect at all (%s)" % (r,))

        def click(x, y, f=25):
            ui.mo.click(x, y)
            m.advance(frames=f)
            m.run()

        # --- 3. a fill box toggles its bit -----------------------------------
        for nm, bit, lbl in (("cs_ckterr", CSFL_TERRAIN, "Terrain"),
                             ("cs_ckbld", CSFL_BLDG, "Buildings")):
            r = rects[nm]
            was = byte("cs_setfill")
            click((r[0] + r[2]) // 2, (r[1] + r[3]) // 2)
            now = byte("cs_setfill")
            check(now == was & ~bit, "the %s box clears its fill bit (%d -> %d)"
                  % (lbl, was, now))
        check(byte("cs_setfill") == 0, "both off is the wireframe (%d)"
              % byte("cs_setfill"))
        for nm in ("cs_ckterr", "cs_ckbld"):
            r = rects[nm]
            click((r[0] + r[2]) // 2, (r[1] + r[3]) // 2)
        check(byte("cs_setfill") == CSFL_ALL, "...and back on again (%d)"
              % byte("cs_setfill"))

        # --- 2. Buildings = Few, on the page ---------------------------------
        #
        # AND THE LIST HAS TO COME DOWN ON THE GLASS. The pick alone is not
        # the check: os88ui_drpress marks the record OPEN before it arms the
        # clip, so a record with no OS88UI_DR_WIN takes the press, draws
        # NOTHING, and the second click still lands on an item rect and picks
        # it - which is how this row passed while the page's four drop-downs
        # could not be dropped down at all (88.13.6). The proofs are the bank
        # (OS88UI_DR_SEG is non-zero only on the path that drew the list) and
        # the pixels under the box.
        r = rects["cs_drbld"]
        m.pause()
        _, _, was = m.vram()
        m.run()
        click((r[0] + r[2]) // 2, (r[1] + r[3]) // 2)
        drseg = int.from_bytes(m.readseg(seg, mp["cs_drbld"] + 18, 2), "little")
        check(m.readseg(seg, mp["cs_drbld"] + 16, 1)[0] == 1,
              "the press opens the Buildings list")
        check(drseg != 0,
              "...and it BANKED what it covered, which only the path that "
              "drew it does (%04x)" % drseg)
        m.pause()
        _, _, now = m.vram()
        m.run()
        # ...and WHERE it is drawn is OS88UI_DR_TOP (SPEC.md 13.14.2), not the
        # row under the box: a list too tall for the room below its control
        # slides UP into the window. Buildings HAS a fourth item now (None,
        # 88.13.1) and the others have three, so reading the record is what
        # keeps this row true - which is exactly what it was written for.
        top = int.from_bytes(m.readseg(seg, mp["cs_drbld"] + 22, 2), "little")
        drew = sum(sum(1 for x in range(r[0], r[2] + 1)
                       if was[y][x] != now[y][x])
                   for y in range(top, min(top + 5 * 12 + 2, len(was))))
        check(drew > 200, "...and the list is ON THE GLASS where os88ui_drfit "
                          "put it (%d pixels changed)" % drew)
        click(r[0] + 20, top + 1 + 2 * 12 + 6)      # the THIRD item: None and
        check(byte("cs_setbld") == CSBL_LOW,        # Only Roads are above it
              "picking Low sets the detail level (%d)" % byte("cs_setbld"))

        # --- 2b. Done is a BUTTON: down on the press, fired at the release --
        d = rects["cs_donerect"]
        cx, cy = (d[0] + d[2]) // 2, (d[1] + d[3]) // 2

        def press(x, y):
            ui.mo.to(x, y)
            ui.mo._edge(True)
            m.advance(frames=20)
            m.run()

        def release(x, y):
            ui.mo.to(x, y)
            ui.mo._edge(False)
            m.advance(frames=40)
            m.run()

        press(cx, cy)
        check(byte("cs_donedn") == 1, "Done is drawn DOWN while it is held")
        check(byte("cs_page") == 2, "...and the press alone does not turn the "
                                    "page (%d)" % byte("cs_page"))
        release(d[0] - 60, d[1] - 40)
        check(byte("cs_page") == 2 and byte("cs_donedn") == 0,
              "a release off the button is a cancel (page %d, down %d)"
              % (byte("cs_page"), byte("cs_donedn")))
        press(cx, cy)
        release(cx, cy)
        check(byte("cs_page") == 0,
              "...and pressed and released on it, Done turns the page (%d)"
              % byte("cs_page"))
        ui.menu_pick("Flight", "Settings")
        m.advance(frames=40)
        m.run()

        # --- into the bracket, where the work is measurable ------------------
        rd = mp["cs_render"]

        seen = {}

        def skipclr():
            """Every object due to be looked at AGAIN, now.

            CSO_SKIP is not a flag, it is THE TICK THE CULL NEXT LOOKS AT THIS
            OBJECT (SPEC.md 88.5.2), so what one frame files is what the cull
            examined - and an object still inside its deferral is absent from
            the count whatever the rung says. The guest free-runs between
            every pause here, for a HOST-timing-dependent number of frames,
            so a count taken without this reads the deferral phase rather
            than the rung: measured at six-way concurrency the None rung read
            0 filed with 6 objects deferred in 3 runs of 12, and 3 filed with
            3 deferred in the other 9 - the same pose, the same world, the
            same cs_setbld, and a re-clear brought it straight back to 3.
            """
            ap = int.from_bytes(m.read(lin + base + off("cs_airport"), 2),
                                "little")
            objs = int.from_bytes(m.read(lin + ap + CSA_OBJS, 2), "little")
            nobj = int.from_bytes(m.read(lin + ap + CSA_NOBJ, 2), "little")
            for o in range(objs, objs + nobj * CSO_SIZE, CSO_SIZE):
                m.write(lin + o + CSO_SKIP, b"\x00\x00")
            m.write(lin + mp["cs_rwobj"] + CSO_SKIP, b"\x00\x00")

        def frames(n=6):
            """Milliseconds a frame, and what the cull filed - READ AT THE
            STOP, because cs_nvisn is zeroed at the top of every cs_scene and
            a read taken while the guest runs catches it part way up."""
            m.pause()
            skipclr()
            m.run()
            m.bp_exec(lin + rd)
            m.run()
            if m.wait_stop(30) is None:
                sys.exit("skiesset: cs_render never ran")
            c0 = m.status()["cycles"]
            out = []
            for _ in range(n):
                m.run()
                if m.wait_stop(30) is None:
                    sys.exit("skiesset: the frame never came")
                c1 = m.status()["cycles"]
                out.append((c1 - c0) / CPS * 1000.0)
                c0 = c1
            seen["n"] = w("cs_nvisn")
            m.bp_exec()
            m.run()
            return sum(out) / len(out)

        POSE = (("cs_px", 150), ("cs_py", 300), ("cs_pz", -900))

        def pin():
            """Over the city, the world paused, every skip cleared.

            THE PAUSE GOES ON FIRST AND A FRAME IS LET BY (docs/WRITING-TESTS
            13 row 35). `m.pause()` can land in the middle of `cs_step`, and
            that step finishes when the guest resumes - writing its own
            cs_px/py/pz over the pose this just wrote. The aeroplane is then
            somewhere else, and at Low detail somewhere else is a different
            number of objects: it read 4 in one run and 11 in the next off
            the same script. cs_pause is tested at the top of the sim loop
            (SPEC.md 88.13.5), so once a frame has gone by with it set no
            step is in flight and the pose stands. Then it is CONFIRMED,
            because a pin nothing reads back is a pin nothing has."""
            m.pause()
            m.write(lin + base + off("cs_pause"), b"\x01")
            m.run()
            m.advance(frames=2)
            m.pause()
            for nm, v in POSE:
                m.write(lin + base + off(nm), ((v * 256) & 0xFFFFFFFF).to_bytes(4, "little"))
            m.write(lin + base + off("cs_hdg"), (30 * 65536 // 360).to_bytes(2, "little"))
            m.write(lin + base + off("cs_pitch"),   # NOSE DOWN, so the view is
                    ((-20 * 65536 // 360) & 0xFFFF).to_bytes(2, "little"))
            m.write(lin + base + off("cs_state"), b"\x01")
            # THE WORLD IS THE PICKED LOCATION'S since SPEC.md 88.6.4, so the
            # skips to clear are the ones in the table its record names and
            # not a global cs_objtab, which no longer exists.
            skipclr()
            m.run()
            m.advance(frames=2)
            m.pause()
            got = [int.from_bytes(m.read(lin + base + off(nm), 4), "little",
                                  signed=True) for nm, _ in POSE]
            m.run()
            want = [v * 256 for _, v in POSE]
            if got != want:
                sys.exit("skiesset: the pose did not take - %s against %s. "
                         "The world was still stepping when it was written "
                         "(docs/WRITING-TESTS.md 13 row 35)" % (got, want))

        m.type_text("f")                            # off the page first: any
        m.advance(frames=40)                        # key returns to the title
        m.run()
        for _ in range(20):                         # ...and LEAVING THE PAGE
            if byte("cs_page") == 0:                # NOW WRITES A FILE
                break                               # (88.13.9): a floppy
            m.advance(frames=40)                    # create is several
            m.run()                                 # int 13h calls and forty
                                                    # frames does not cover it
        check(byte("cs_page") == 0, "a key comes back from the page (%d)"
              % byte("cs_page"))
        m.type_text("f")
        m.advance(frames=80)
        m.run()
        check(byte("cs_back") != 0, "the bracket took a mode")
        pin()
        frames(3)
        few_ms = frames()
        few_n = seen["n"]
        bld(CSBL_HIGH)                              # ...F1 steps the detail level
        m.advance(frames=40)
        m.run()
        pin()
        frames(3)
        all_ms = frames()
        all_n = seen["n"]
        check(byte("cs_setbld") == CSBL_HIGH,
              "F5 is Detail Level = High (%d)" % byte("cs_setbld"))
        check(few_n < all_n, "Low files fewer objects than High (%d against %d)"
              % (few_n, all_n))

        # --- 3a. MODERATE IS HIGH MINUS CSO_DENSE (SPEC.md 88.13.1.1) -------
        #
        # Counted rather than compared as pixels: a paused Clear Skies is not
        # a still picture - the water moves - so two arms drawing the same
        # objects differ on the glass by thousands of pixels, and a same-rung
        # CONTROL reads the same thousands. The cull's own count is exact.
        #
        # SELF-CONTAINED, and it puts the world back. The default location is
        # Paris-Issy and nothing there is CSO_DENSE, so a check that stayed on
        # it would only ever take its equal branch - and the other branch is
        # the one the rung exists for. The world with a dense city is found by
        # walking cs_ports, so this does not go stale when a second location
        # grows one, and everything after here still runs on the world the
        # checks above measured.
        def dense_in(rec):
            objs = int.from_bytes(m.read(lin + rec + 18, 2), "little")
            nobj = int.from_bytes(m.read(lin + rec + 20, 2), "little")
            return sum(1 for o in range(objs, objs + nobj * 20, 20)
                       if int.from_bytes(m.read(lin + o + 16, 2), "little")
                       & 0x0200)

        def go(rec):
            """Fly `rec`'s world - by POKING cs_airport, without leaving.

            cs_scene reads [cs_airport] every frame, so the object table
            switches at once; only cs_runway_build and the aeroplane's start
            are behind, and neither matters to a count taken at three rungs
            with the camera pinned - the stale runway is one object present
            identically in all three. The first version toggled the bracket
            with F and fixed frame advances, and F TOGGLES: one that had not
            landed left the row on the other side of the bracket, and every
            check after it read a world that was not being drawn. Under a
            loaded lane that is what happened, Detail Level = None filing 0
            objects because nothing was flying.
            """
            m.pause()
            m.write(lin + base + off("cs_airport"), rec.to_bytes(2, "little"))
            m.run()

        def at_level(k):
            bld(k)
            m.advance(frames=40)
            m.run()
            pin()
            frames(3)
            frames()
            return seen["n"]

        def rungs(rec, who):
            nd = dense_in(rec)
            hi_n, mod_n, low_n = (at_level(CSBL_HIGH), at_level(CSBL_MOD),
                                  at_level(CSBL_LOW))
            check(low_n <= mod_n <= hi_n,
                  "%s: the rungs nest, Low %d <= Moderate %d <= High %d"
                  % (who, low_n, mod_n, hi_n))
            if nd:
                check(mod_n < hi_n,
                      "%s has %d CSO_DENSE objects, so Moderate files "
                      "strictly fewer than High (%d against %d)"
                      % (who, nd, mod_n, hi_n))
            else:
                check(mod_n == hi_n,
                      "%s wears no CSO_DENSE, so Moderate and High file the "
                      "SAME set (%d and %d)" % (who, mod_n, hi_n))
            return nd

        # BOTH WORLDS, because the two branches catch different things. The
        # EQUAL branch is what a broken ladder trips - a top-rung filter that
        # also fires at Moderate makes the two counts differ where nothing is
        # dense - and the STRICT branch is what says the flag reaches the cull
        # at all. Neither alone is enough: --clobber-dense drops collidables
        # at Low and Moderate together, so on the dense world the counts still
        # nest and still differ, and only the default world's equality sees it.
        # ...AND SINCE 88.13.1.3 EVERY WORLD HAS A DENSE CITY, so the equal
        # branch has no location left to stand on and would silently stop
        # running - which is how a gate goes green having tested half of
        # itself. It is SYNTHESISED instead: the CSO_DENSE bit is cleared in
        # the guest's own table, the three rungs are read again and the bits
        # go back. That is stronger than the world that used to provide it,
        # because it holds whichever world the row is on rather than the one
        # that happened to have no towers.
        def dense_bits(rec, on):
            objs = int.from_bytes(m.read(lin + rec + 18, 2), "little")
            nobj = int.from_bytes(m.read(lin + rec + 20, 2), "little")
            m.pause()
            for o in range(objs, objs + nobj * 20, 20):
                f = int.from_bytes(m.read(lin + o + 16, 2), "little")
                if on and o in dense_was:
                    f |= 0x0200
                elif not on and f & 0x0200:
                    dense_was.add(o)
                    f &= ~0x0200
                m.write(lin + o + 16, f.to_bytes(2, "little"))
            m.run()

        home = int.from_bytes(m.read(lin + base + off("cs_airport"), 2), "little")
        rungs(home, "the default location")
        dense_was = set()
        dense_bits(home, False)
        check(dense_in(home) == 0, "the dense bits come off the table for the "
              "equal branch (%d left)" % dense_in(home))
        rungs(home, "the same world with its dense bits off")
        dense_bits(home, True)
        nport = int.from_bytes(m.readseg(seg, mp["cs_drport"] + 10, 2), "little")
        for i in range(nport):
            rec = int.from_bytes(m.readseg(seg, mp["cs_ports"] + 2 * i, 2),
                                 "little")
            if rec != home and dense_in(rec):
                go(rec)
                rungs(rec, "another world with a dense city")
                go(home)
                break
        bld(CSBL_HIGH)
        m.advance(frames=40)
        m.run()

        # --- 3c. A CSO_DENSE building is NOT SOLID below High (88.13.1.4) ----
        #
        # cs_collide walks the location's table and tests CSO_COLLIDE, and it
        # never consulted the Detail Level: a High tier is content that does
        # not exist below High, so without the gate every one of those towers
        # kills a player at the DEFAULT rung, out of clear air, in a world
        # that draws nothing there. The aeroplane is put at the centre of one
        # at half its height and flown for a few ticks.
        #
        # WHICH ONE is chosen host-side: the dense object whose centre is
        # furthest inside no OTHER collidable's box, because a crash into
        # something that was going to kill it anyway proves nothing.
        objs = int.from_bytes(m.read(lin + home + 18, 2), "little")
        nobj = int.from_bytes(m.read(lin + home + 20, 2), "little")

        def obj(o):
            md = int.from_bytes(m.read(lin + o + 0, 2), "little")   # CSO_MODEL
            typ = m.readseg(seg, md, 1)[0]
            vp = int.from_bytes(m.readseg(seg, md + 8, 2), "little")
            nv = m.readseg(seg, md + 1, 1)[0]
            sw = lambda v: v - 65536 if v >= 32768 else v      # noqa: E731
            top = max(sw(int.from_bytes(m.readseg(seg, vp + 6 * L + 2, 2), "little"))
                      for L in range(nv))
            return dict(typ=typ,
                        x=sw(int.from_bytes(m.read(lin + o + 4, 2), "little")),
                        z=sw(int.from_bytes(m.read(lin + o + 6, 2), "little")),
                        hx=abs(sw(int.from_bytes(m.readseg(seg, vp, 2), "little"))),
                        hz=abs(sw(int.from_bytes(m.readseg(seg, vp + 4, 2), "little"))),
                        top=top,
                        name=int.from_bytes(m.read(lin + o + 14, 2), "little"),
                        flags=int.from_bytes(m.read(lin + o + 16, 2), "little"))
        rows = [obj(o) for o in range(objs, objs + nobj * 20, 20)]
        # A STACK only: a CSM_FLAT model's "level 0" is its first VERTEX and
        # not a footprint, so a river read as a solid is a box the size of the
        # map and every candidate reads as buried in it.
        solid = [r for r in rows if r["flags"] & 0x0001 and r["typ"] == CSM_STACK
                 and not r["flags"] & 0x0200]

        def lonely(r):
            return min([max(abs(r["x"] - q["x"]) - q["hx"],
                            abs(r["z"] - q["z"]) - q["hz"]) for q in solid] or [1e9])
        pick = max((r for r in rows if r["flags"] & 0x0200 and r["flags"] & 0x0001
                    and r["typ"] == CSM_STACK), key=lonely, default=None)
        check(pick is not None and lonely(pick) > 50,
              "there is a CSO_DENSE building to sit inside, %d m clear of "
              "every other solid thing" % (lonely(pick) if pick else -1))
        if pick:
            nm = m.readseg(seg, pick["name"], 32).split(b"\0")[0].decode(
                "ascii", "replace")

            def sit(rung):
                bld(rung)
                m.advance(frames=40)
                m.run()
                m.pause()
                m.write(lin + base + off("cs_pause"), b"\x01")
                m.run()
                m.advance(frames=2)
                m.pause()
                for k, v in (("cs_px", pick["x"]), ("cs_py", pick["top"] // 2),
                             ("cs_pz", pick["z"])):
                    m.write(lin + base + off(k),
                            ((v * 256) & 0xFFFFFFFF).to_bytes(4, "little"))
                for k in ("cs_spd", "cs_thr", "cs_vs", "cs_hs",
                          "cs_pitch", "cs_roll"):
                    m.write(lin + base + off(k), b"\x00\x00")
                m.write(lin + base + off("cs_state"), b"\x01")
                m.write(lin + base + off("cs_pause"), b"\x00")
                rung_now = byte("cs_setbld")
                c0 = w("cs_crashes")
                m.run()
                m.advance(frames=25)
                m.pause()
                out = (byte("cs_state"), w("cs_crashes") - c0,
                       w("cs_crashwhy"), rung_now)
                m.run()
                return out

            st, dc, why, rn = sit(CSBL_MOD)
            check(rn == CSBL_MOD and st != 2 and dc == 0,
                  "at Moderate (rung %d) the aeroplane flies through %s at "
                  "(%d,%d): state %d, %d crashes"
                  % (rn, nm, pick["x"], pick["z"], st, dc))
            st, dc, why, rn = sit(CSBL_HIGH)
            check(rn == CSBL_HIGH and st == 2 and dc == 1 and why == pick["name"],
                  "...and at High (rung %d) it hits it, by name: state %d, "
                  "%d crashes, why %04x against %s at %04x"
                  % (rn, st, dc, why, nm, pick["name"]))
            m.advance(frames=60)           # let the crash reset before 3b
            m.run()
        bld(CSBL_HIGH)
        m.advance(frames=40)
        m.run()

        # --- 3b. Buildings = None files FEWER STILL, and keeps the world -----
        #
        # None is a density and not a fill: it refuses in cs_consider before
        # any transform (88.13.1), which is the cheapest form there is. What
        # it must NOT refuse is TERRAIN - the hills, the water and the runway
        # carry CSO_TERRAIN and are the world's surface, not scenery to thin
        # out. Then Only Roads brings back the roads and bridges and nothing
        # else, which is the rung between. IN THE BRACKET, because on the
        # Settings page any key turns the page back and the F-key is spent
        # doing that.
        bld(CSBL_NONE)
        m.advance(frames=40)
        m.run()
        check(byte("cs_setbld") == CSBL_NONE,
              "F1 steps down to Detail Level = None (%d)" % byte("cs_setbld"))
        pin()
        frames(3)
        none_ms = frames()
        none_n = seen["n"]
        check(none_n < few_n, "None files fewer than Low (%d against %d)"
              % (none_n, few_n))
        # ...and if it IS empty, say what it was looking at. An empty world
        # here has never been the rung: it is a pose that did not take or a
        # cull that never looked, and neither is visible in a bare count.
        m.pause()
        ap_now = int.from_bytes(m.read(lin + base + off("cs_airport"), 2),
                                "little")
        st_now = byte("cs_state")
        m.run()
        check(none_n > 0, "...and not an empty world: the runway, the water "
                          "and the terrain are still filed (%d, state %d, "
                          "airport %04x)" % (none_n, st_now, ap_now))
        # AGAINST FULL and not against Low: by Low the frame is already the
        # ground band and a few distant objects, so None against Low is a few
        # per cent either way - under this harness's own spread - and a check
        # that asserts it is a check that fails on nothing.
        check(none_ms < all_ms * 0.6, "...and its frame is far shorter than "
              "Full's (%.1f ms against %.1f)" % (none_ms, all_ms))

        bld(CSBL_ROADS)                             # ...and ONLY ROADS brings
        m.advance(frames=40)                        # the roads back, no more
        m.run()
        check(byte("cs_setbld") == CSBL_ROADS,
              "...and the next rung up is Only Roads (%d)" % byte("cs_setbld"))
        pin()
        frames(3)
        frames()
        roads_n = seen["n"]
        check(none_n < roads_n < few_n,
              "Only Roads files more than None and fewer than Low "
              "(%d, %d, %d)" % (none_n, roads_n, few_n))
        bld(CSBL_HIGH)                              # ...and everything back
        m.advance(frames=40)
        m.run()

        # --- 4. the hotkeys CYCLE, one key a setting (88.13.5) ---------------
        #
        # It was a key a VALUE and that ran out - eleven keys for four
        # settings, and twelve exist. Each of these steps its own ladder one
        # rung and ROUND at the top, which is the whole of what has to be
        # checked: a full lap, so that both the step and the wrap are seen.
        def press(key, watch=None):
            # FORTY and not twenty: m.advance counts DISPLAY frames and
            # cs_input polls int 16h once a RENDER frame, which here is 150 to
            # 400 ms. At twenty a press landed inside the NEXT check's window
            # and read as the one before it having done nothing.
            #
            # ...and where the caller can say WHICH byte the key moves, the
            # press is CONFIRMED rather than timed: forty frames is enough
            # most of the time, and "most of the time" in a row of forty
            # presses is a flake a lane will find.
            was = byte(watch) if watch else None
            m.key(key)
            m.advance(frames=40)
            m.run()
            for _ in range(8):
                if watch is None or byte(watch) != was:
                    break
                m.advance(frames=40)
                m.run()
            m.pause()
            out = (byte("cs_setbld"), byte("cs_setlod"), byte("cs_setsize"),
                   byte("cs_setfill"), byte("cs_msg"), byte("cs_toastt"),
                   m.readseg(seg, base + off("cs_toastbuf"), 40)
                   .split(b"\0")[0].decode("ascii", "replace"))
            m.run()
            return out
        for key, name, rungs in (("F1", "cs_setbld", CSBL_HIGH + 1),
                                 ("F2", "cs_setlod", CSL_ULTRA + 1),
                                 ("F3", "cs_setsize", CSZ_FULL + 1)):
            was = byte(name)
            lap = []
            for _ in range(rungs):
                lap.append(press(key, name)[("cs_setbld", "cs_setlod",
                                             "cs_setsize").index(name)])
            check(lap == [(was + 1 + i) % rungs for i in range(rungs)],
                  "%s steps %s one rung and round: %s from %d"
                  % (key, name, lap, was))
            check(byte(name) == was,
                  "...and a full lap of %d comes home (%d against %d)"
                  % (rungs, byte(name), was))
        for key, bit in (("F4", CSFL_TERRAIN), ("F5", CSFL_BLDG)):
            was = byte("cs_setfill")
            press(key, "cs_setfill")
            check(byte("cs_setfill") == was ^ bit,
                  "%s toggles its own fill bit (%d -> %d)"
                  % (key, was, byte("cs_setfill")))
            press(key, "cs_setfill")
            check(byte("cs_setfill") == was, "...and back (%d)" % byte("cs_setfill"))

        # --- 4c. and every one of them TOASTS what it changed (88.13.8) ------
        #
        # A cycling key is no good if the glass does not say where the ladder
        # landed, so this is the feature and not a decoration. The text is
        # lettered out of the Settings page's own strings, so it is checked
        # against the page's OWN list - cs_i_lod's names read out of the
        # guest - rather than against a copy typed here that could agree with
        # a wrong answer.
        def dropname(tab, i):
            p = int.from_bytes(m.readseg(seg, mp[tab] + 2 * i, 2), "little")
            return m.readseg(seg, p, 24).split(b"\0")[0].decode(
                "ascii", "replace").upper()
        for key, name, tab, label in (("F1", "cs_setbld", "cs_i_bld", "DETAIL LEVEL"),
                                      ("F2", "cs_setlod", "cs_i_lod", "DRAW DISTANCE"),
                                      ("F3", "cs_setsize", "cs_i_size", "SIZE")):
            st = press(key, name)
            want = "%s: %s" % (label, dropname(tab, byte(name)))
            check(st[4] == CSG_TOAST and st[6] == want,
                  "%s toasts %r (msg %d, %r)" % (key, want, st[4], st[6]))
            if key == "F3":                 # ...and put the size back: check 5
                to_rung("F3", "cs_setsize", CSZ_MOD, CSZ_FULL + 1)
        # A FILL IS FILL OR WIRE and not on or off: a cleared bit is a
        # WIREFRAME and not a thing gone (88.13.3), and the toast is where
        # that distinction reaches the player.
        for key, bit, label in (("F4", CSFL_TERRAIN, "TERRAIN"),
                                ("F5", CSFL_BLDG, "BUILDINGS")):
            st = press(key, "cs_setfill")
            want = "%s: %s" % (label, "FILL" if st[3] & bit else "WIRE")
            check(st[4] == CSG_TOAST and st[6] == want,
                  "%s toasts %r (%r)" % (key, want, st[6]))
            press(key, "cs_setfill")        # ...and back
        # AND A SECOND TOAST REPAINTS THE STRIP. cs_k_msg is the panel's key
        # for it and it was (cs_msg, the low byte of cs_crashwhy) - identical
        # for two toasts in a row, so the second setting changed under a line
        # still naming the first. The count in the key is what fixed it, and
        # the check is that the PAINTER runs, not that the byte moved.
        press("F1")
        m.bp_exec(lin + mp["cs_d_msg"])
        m.run()
        m.key("F2")
        ran = m.wait_stop(30) is not None
        m.bp_exec()
        m.run()
        check(ran, "a second toast repaints the strip (cs_d_msg %s)"
                   % ("ran" if ran else "never ran"))
        # ...and it goes away again, back to what the strip was saying
        m.pause()
        m.write(lin + base + off("cs_msg"), bytes([CSG_TAKEOFF]))
        m.run()
        m.advance(frames=6)
        m.run()
        st = press("F1")
        check(st[4] == CSG_TOAST and st[5] > 0,
              "a toast is up with %d ticks to run" % st[5])
        for _ in range(40):
            m.advance(frames=8)
            m.run()
            if byte("cs_msg") != CSG_TOAST:
                break
        check(byte("cs_msg") == CSG_TAKEOFF,
              "...and it expires back to the strip it replaced (msg %d, "
              "toastt %d)" % (byte("cs_msg"), byte("cs_toastt")))
        while byte("cs_setbld") != CSBL_HIGH:      # the rung the checks below
            press("F1")                            # were taken at

        # --- 5. a smaller view leaves nothing beside it ----------------------
        #
        # READ VRAM, NOT THE RENDERED FRAME. MartyPC's Hercules raster does
        # not land on the framebuffer's origin - measured at (-16, +2) on the
        # mode this kernel sets - so a band named in BOX coordinates and read
        # out of m.fbuf() is sixteen pixels adrift, which puts the view's own
        # left edge inside it and reads as a bleed beside the picture that is
        # not there at all (docs/MARTYPC-DEBUG.md, "the rendered frame is not
        # the framebuffer"). m.vram() is byte-for-byte the card's memory.
        pin()
        frames(3)
        big = (w("cs_ww"), w("cs_wh"))
        vx, vy = w("cs_vx"), w("cs_vy")
        back = byte("cs_back")
        bpp = 2 if back == 2 else 1             # CGA packs two bits a pixel
        m.pause()
        _, _, was = m.vram()
        m.run()

        def band(rows, wx0, wh):
            """Every pixel of the box LEFT of the view, lit ones counted."""
            return sum(sum(rows[y][vx * bpp:(vx + wx0) * bpp])
                       for y in range(vy, vy + wh))

        m.type_text("-")
        m.advance(frames=80)
        m.run()
        pin()
        frames(4)
        small = (w("cs_ww"), w("cs_wh"))
        check(small[0] * 2 == big[0] and small[1] * 2 == big[1],
              "the - key halves the view (%dx%d -> %dx%d)" % (big + small))
        m.pause()
        _, _, fb = m.vram()
        m.run()
        wx0 = w("cs_wx0")
        before, after = band(was, wx0, small[1]), band(fb, wx0, small[1])
        check(before > 100,
              "the larger view really did put something there (%d lit)" % before)
        check(after == 0, "the larger view is gone from beside the smaller "
              "one (%d lit)" % after)
        m.type_text("+")
        m.advance(frames=60)
        m.run()
        m.type_text("f")
        m.advance(frames=40)
        m.run()

        # --- 6. the page's answer SURVIVES A CLOSE (88.13.9) ----------------
        #
        # Picked on the page, left by Done, the window closed, the package
        # opened again - which is the whole feature, and the only way to
        # check it is the round trip: the file is written by one instance and
        # read by another, so an in-memory check would pass on a save that
        # never reached the disk and on a load that never ran.
        #
        # THE HOTKEYS ARE NOT PART OF IT and must not be: they are
        # deliberately temporary (88.13.5), so this drives the PAGE.
        ui.menu_pick("Flight", "Settings")
        m.advance(frames=40)
        m.run()
        check(byte("cs_page") == 2, "Flight -> Settings for the round trip "
                                    "(page %d)" % byte("cs_page"))
        WANT = ((CSBL_LOW, "cs_setbld"), (CSL_ULTRA, "cs_setlod"),
                (CSZ_SMALL, "cs_setsize"), (CSFL_TERRAIN, "cs_setfill"))
        m.pause()
        for v, nm in WANT:
            m.write(lin + base + off(nm), bytes([v]))
        m.run()
        m.advance(frames=20)
        m.run()
        m.pause()
        dr = [int.from_bytes(m.readseg(seg, mp["cs_donerect"] + 2 * i, 2),
                             "little") for i in range(4)]
        m.run()
        os88mouse.Mouse(marty=m).click((dr[0] + dr[2]) // 2, (dr[1] + dr[3]) // 2)
        m.advance(frames=80)
        m.run()
        check(byte("cs_page") == 0, "...Done leaves the page (page %d)"
                                    % byte("cs_page"))
        ui.menu_pick("Clear Skies", "Close")
        m.advance(frames=60)
        m.run()
        ui.path("B:/GAMES/SKIES.O88")
        slot, seg = dispapps.pkg_seg(m, 0)
        lin = seg << 4
        base = int.from_bytes(m.readseg(seg, 8, 2), "little")
        m.advance(frames=60)
        m.run()
        got = tuple(byte(nm) for _, nm in WANT)
        check(got == tuple(v for v, _ in WANT),
              "...and the new instance comes back with them: %s, wanted %s"
              % (got, tuple(v for v, _ in WANT)))

    if bad:
        for b in bad:
            print("FAIL: " + b)
        return 1
    print("  ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
