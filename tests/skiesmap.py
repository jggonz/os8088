#!/usr/bin/env python3
"""THE MAP - M in flight (SPEC.md 88.16), on MartyPC.

    python3 tests/skiesmap.py [--machine os8088_5150_herc_gla]

M steps the view OFF -> NEAR -> FAR -> OFF, north up and centred on the
aeroplane, and Q is the engine sound (it was M). Checked off the guest:

  1. each M moves [cs_map] one rung and round at the top, and the strip says
     which - `MAP: NEAR`, `MAP: FAR`, `MAP: OFF`, composed in cs_toastbuf;
  2. while the map is up cs_mapview runs and cs_scene does NOT - the map is
     the view and not something drawn over it - and with it off again the
     scene is back;
  3. the two scales are two different pictures: the view's shadow bytes at
     NEAR and at FAR differ, and the map's at all from the 3D view's;
  4. Q toggles [cs_sound] and M no longer does.

The red run is by hand and recorded in SPEC.md 88.16: M put back on the mute
(the map unreachable) fails 1, 2 and 4.
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
bad = []


def check(cond, what):
    print("  [%s] %s" % ("PASS" if cond else "FAIL", what))
    if not cond:
        bad.append(what)


def equ(path, name):
    m = re.search(r"^%s\s+equ\s+(-?\w+)" % name, open(path).read(), re.M)
    if not m:
        raise SystemExit("skiesmap: %s is not defined in %s" % (name, path))
    return int(m.group(1), 0)


def lift_leg(m, seg, base, lin, mp, byte, w, no_lift):
    """5. IN A SAILPLANE THE MAP SHOWS THE LIFT (88.16.1), where the guest's
    own air says it is - in a tile five east of the release, which is variant
    7 (transposed and mirrored both ways), so a box drawn by the table as
    written would be somewhere else - and in an aeroplane with an engine it
    shows nothing."""
    asm = os.path.join(ROOT, "apps", "skies", "skies.asm")
    fl = os.path.join(ROOT, "apps", "skies", "csflight.inc")
    T, NL = equ(fl, "CS_AIRTILE"), equ(fl, "CS_NLIFT")
    thr, lift = equ(asm, "CSP_THRUST"), equ(asm, "CSI_LIFT")

    def off(n):
        return dispapps.bss_off("skies", n)

    def poke(n, d):
        m.write(lin + base + off(n), d)

    def rd(at, n=2):
        return int.from_bytes(m.read(lin + at, n), "little")

    def sg(v):
        return v - 65536 if v >= 32768 else v
    rows = [[sg(rd(mp["cs_lifts"] + 10 * i + 2 * k)) for k in range(5)]
            for i in range(NL)]
    dx, dz, hw, hd, rate = max(rows, key=lambda r: r[4])
    tx, tz = 5, 0
    v = ((tx * 3) ^ (tz * 5)) & 7
    if v & 4:                                   # cs_maptile's inverse
        dz = T - dz
    if v & 2:
        dx = T - dx
    if v & 1:
        dx, dz, hw, hd = dz, dx, hd, hw
    planes = [rd(mp["cs_planes"] + 2 * i) for i in range(5)]
    glider = [p for p in planes if rd(p + thr) == 0]
    if not glider:
        sys.exit("skiesmap: no aeroplane in cs_planes has CSP_THRUST 0")
    shown = {}
    for name, plane in (("Bijave", glider[0]), ("Cessna", planes[0])):
        m.pause()
        poke("cs_plane", plane.to_bytes(2, "little"))
        poke("cs_inited", b"\x00")
        m.run()
        m.type_text("f")
        m.advance(frames=100)
        m.run()
        m.type_text("m")                            # NEAR
        m.advance(frames=40)
        m.run()
        ax, az = sg(w("cs_airx")), sg(w("cs_airz"))
        px = ax + tx * T - T // 2 + dx              # the rect's centre...
        pz = az + tz * T - T // 2 + dz
        if name == "Bijave":                        # ...whose air is lift
            m.pause()
            for n, val in (("cs_px", px), ("cs_pz", pz), ("cs_py", 900)):
                poke(n, ((val * 256) & 0xFFFFFFFF).to_bytes(4, "little"))
            poke("cs_state", b"\x01")
            poke("cs_airv", b"\x00\x00")          # ...and read only what a
            m.run()                                 # step AFTER the pin wrote:
            for _ in range(20):                     # a frame is several ticks
                m.advance(frames=10)                # on CGA, so a fixed wait
                m.run()                             # can end before one
                if w("cs_airv"):
                    break
            airv = sg(w("cs_airv"))
            print("      the air at the rect's centre in tile (%d, %d), variant "
                  "%d: %+d for a core of %+d" % (tx, tz, v, airv, rate))
            check(airv > rate * 3 // 4,
                  "the guest's air at the variant's rect is that rect's lift "
                  "(%+d for %+d)" % (airv, rate))
        # THE PAUSE GOES ON FIRST AND A FRAME IS LET BY (docs/WRITING-TESTS.md
        # 13 row 22): a step in flight at m.pause() finishes on resume and
        # writes its own position over the pin
        m.pause()
        poke("cs_pause", b"\x01")
        m.run()
        m.advance(frames=20)
        m.pause()                                   # the eye 24 px east of it
        for n, val in (("cs_px", px + 24 * 16), ("cs_pz", pz),
                       ("cs_py", 900)):
            poke(n, ((val * 256) & 0xFFFFFFFF).to_bytes(4, "little"))
        poke("cs_hdg", (16384).to_bytes(2, "little"))
        poke("cs_state", b"\x01")
        poke("cs_pause", b"\x01")
        if no_lift:                                 # the red run: never drawn
            m.write(lin + mp["cs_mapair"], b"\xC3")
        m.run()
        m.advance(frames=60)
        # ...AND THE SHADOW IS READ AT A FRAME'S END: a pause lands anywhere,
        # and between the ground's erase and the lift's fill the rows under
        # test are ground - which read as a map that drew no lift one run in
        # three. cs_r_end is the frame finished and not yet blitted
        m.bp_exec(lin + mp["cs_r_end"])
        m.run()
        if m.wait_stop(30) is None:
            sys.exit("skiesmap: no frame reached cs_r_end")
        m.bp_exec()
        tseg, tbase = w("cs_tseg"), w("cs_tbase")
        ks, vcx, vcy = byte("cs_kshift"), sg(w("cs_vcx")), sg(w("cs_vcy"))
        pat = bytes(m.read(lin + w("cs_inktab") + lift * 4, 4))
        got = []                                    # the EVEN rows: the
        for y in range(vcy - 4, vcy + 4, 2):        # odd ones keep the ground
            ro = rd(base + off("cs_rowoff") + 2 * y) + tbase
            got.append((m.readseg(tseg, ro + ((vcx - 24) >> ks), 1)[0],
                        pat[y & 3]))
        shown[name] = all(g == p for g, p in got)
        if not shown[name] or os.environ.get("SKIESMAP_DIAG"):
            print("      (the guest: px %d pz %d, eye %d %d, want %d %d; map %d "
                  "pause %d state %d; tiles x %d..%d z %d..%d)"
                  % (sg(int.from_bytes(m.readseg(seg, base + off("cs_px") + 1,
                                                 2), "little")),
                     sg(int.from_bytes(m.readseg(seg, base + off("cs_pz") + 1, 2), "little")),
                     sg(w("cs_ex")), sg(w("cs_ez")), px + 384, pz,
                     byte("cs_map"), byte("cs_pause"), byte("cs_state"),
                     sg(w("cs_matx0")), sg(w("cs_matx1")), sg(w("cs_matz")),
                     sg(w("cs_matz1"))))
        print("      %s: four even rows at the rect's centre %s, CSI_LIFT %s"
              % (name, [hex(g) for g, _ in got], [hex(p) for _, p in got]))
        poke("cs_pause", b"\x00")
        m.run()
        m.type_text("m")
        m.type_text("m")                            # NEAR -> FAR -> OFF
        m.type_text("f")
        m.advance(frames=60)
        m.run()
    check(shown["Bijave"],
          "in a sailplane the map shows the lift: CSI_LIFT's pattern where the "
          "air is, through the tile's variant")
    check(not shown["Cessna"],
          "...and in an aeroplane with an engine it does not")


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--no-lift", action="store_true",
                    help="cs_mapair a bare ret: the lift is never drawn: red")
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

        def byte(n):
            return m.readseg(seg, base + off(n), 1)[0]

        def w(n):
            return int.from_bytes(m.readseg(seg, base + off(n), 2), "little")

        def toast():
            b = m.readseg(seg, base + off("cs_toastbuf"), 32)
            return b.split(b"\x00")[0].decode("latin-1")

        def view():
            """The shadow's view bytes: what the raster drew, before the blit"""
            tseg = w("cs_tseg")
            return bytes(m.readseg(tseg, 0, 16000))

        def runs(label, ms):
            m.bp_exec(lin + mp[label])
            m.run()
            hit = m.wait_stop(ms) is not None
            m.bp_exec()
            m.run()
            return hit

        m.advance(frames=30)
        m.run()
        m.type_text("f")
        m.advance(frames=150)
        m.run()
        check(w("cs_back") != 0, "the bracket took a mode")
        flat = byte("cs_back") != 1                 # CSB_MODEX pages the card
        snd0 = byte("cs_sound")
        v3d = view() if flat else None
        check(runs("cs_scene", 20) and not runs("cs_mapview", 3),
              "with the map off the SCENE draws the view")

        seen = []
        shots = {}
        for want, name in ((1, "NEAR"), (2, "FAR"), (0, "OFF")):
            m.type_text("m")
            m.advance(frames=120)
            m.run()
            seen.append((byte("cs_map"), toast()))
            if want:
                check(runs("cs_mapview", 20) and not runs("cs_scene", 3),
                      "at %s cs_mapview draws the view and cs_scene does not"
                      % name)
                if flat:
                    shots[name] = view()
        print("      M, M, M -> %s" % seen)
        check([s[0] for s in seen] == [1, 2, 0],
              "M steps the map off -> near -> far -> off (%s)"
              % [s[0] for s in seen])
        check([s[1] for s in seen] == ["MAP: NEAR", "MAP: FAR", "MAP: OFF"],
              "...and the strip says which (%s)" % [s[1] for s in seen])
        check(runs("cs_scene", 20),
              "with the map off again the scene is back")
        if flat:
            check(shots["NEAR"] != shots["FAR"] and shots["NEAR"] != v3d,
                  "NEAR and FAR are two pictures, and neither is the view")
        check(byte("cs_sound") == snd0,
              "M no longer touches the sound (%d -> %d)"
              % (snd0, byte("cs_sound")))
        m.type_text("q")
        m.advance(frames=30)
        m.run()
        check(byte("cs_sound") == snd0 ^ 1,
              "Q toggles it (%d -> %d)" % (snd0, byte("cs_sound")))
        m.type_text("q")
        m.type_text("f")
        m.advance(frames=40)
        m.run()
        if flat:
            lift_leg(m, seg, base, lin, mp, byte, w, a.no_lift)

    if bad:
        for b in bad:
            print("FAIL: " + b)
        return 1
    print("  ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
