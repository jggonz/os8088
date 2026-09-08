#!/usr/bin/env python3
"""CLEAR SKIES' frame, priced EXACTLY on MartyPC (SPEC.md 88.12).

    python3 tests/skiesperf.py [--machine os8088_5150_herc_gla] [--scene runway|tower|city]

AN INSTRUMENT, NOT A GATE - registered as such in tests/unit/t_registry.py,
and it asserts nothing. tankperf.py's shape (SPEC.md 85.3.4): the frame is
timed by a breakpoint on cs_render, twelve consecutive frames, cycle-exact;
each drawing stage is then priced by patching its call out (NOPs for a
`call`), the sites resolved out of a fresh listing by instruction text, never
by remembered offset; and the SCENE is pinned by poke - the aeroplane parked
in the air at a chosen place and attitude, the world paused - so two builds
price the same frame.

    runway   parked on the runway at Paris-Issy, looking down it (the view
             the flight starts with)
    tower    150 m up, 900 m south-west of the Eiffel Tower, level, nose
             on it: the 32-edge model at its near size
    climb    300 m down the runway at 40 m, nose up 5: the dashed
             centreline (88.6.2) at the height it is meant for
    city     300 m up over the Champ de Mars heading north-east: the tower,
             the Trocadero, the river and the far skyline together
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88marty                                            # noqa: E402
import dispapps                                             # noqa: E402
import skies as skiestest                                   # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CPS = 4772727                           # the 4.77 MHz clock: cycles a second
SCENES = {                              # x, y, z (metres), heading (degrees), pitch
    "runway": None,                     # wherever cs_reset put it
    "tower": (-640, 150, -640, 45, 0),
    "city": (150, 300, -900, 30, -5),
    "climb": (-2689, 40, -2409, 40, 5),  # 300 m down the runway, 40 m up
    "bank": (-2689, 80, -2409, 40, 5, 30),   # ...banked 30 right, for the ADI
}


def listing():
    fd, lst = tempfile.mkstemp(prefix="skiesperf_", suffix=".lst")
    os.close(fd)
    r = subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", "apps/",
                        "-I", "apps/skies/", "-o", os.devnull, "-l", lst,
                        "apps/skies/skies.asm"], capture_output=True, text=True)
    if r.returncode:
        sys.exit("skiesperf: the tree does not assemble:\n" + r.stderr[:400])
    return lst


def sites(lst):
    """Each stage's patch: (addr, the bytes there, what to write) triples,
    found by INSTRUCTION TEXT in the listing."""
    lines = open(lst).read().split("\n")
    rx = re.compile(r"\s*\d+\s+([0-9A-F]{8})\s+([0-9A-F\[\]]+)\s+(?:<\d+>\s*)?(.*)$")
    lab = re.compile(r"\s*\d+\s+(?:[0-9A-F]{8}\s+(?:[0-9A-F()\-\[\]]+\s+)?)?(?:<\d+>\s*)?([A-Za-z_][A-Za-z0-9_]*):")

    def find(pat, nth=0, within=None):
        hits, scope = [], within is None
        for L in lines:
            if within:
                m = lab.match(L)
                if m:
                    scope = (m.group(1) == within)
                if not scope:
                    continue
            m = rx.match(L)
            if m and re.search(pat, m.group(3).split(";")[0].rstrip()):
                b = bytes.fromhex(m.group(2).replace("[", "").replace("]", ""))
                hits.append((int(m.group(1), 16), b))
        if len(hits) <= nth:
            sys.exit("skiesperf: no site matches %r in the listing" % pat)
        return hits[nth]

    def nop(a, b):
        return (a, b, b"\x90" * len(b))
    s = {}
    s["skyground"] = nop(*find(r"call cs_skyground$", within="cs_render"))
    s["scene"] = nop(*find(r"call cs_scene$", within="cs_render"))
    s["  faces"] = nop(*find(r"call cs_faces$", within="cs_drawobj"))
    s["  edges"] = nop(*find(r"call cs_edges$", within="cs_drawobj"))
    s["  verts (stack)"] = nop(*find(r"call cs_stackverts$", within="cs_drawobj"))
    s["  project"] = nop(*find(r"call cs_projall$", within="cs_drawobj"))
    s["  consider x2"] = (nop(*find(r"call cs_consider$", 0, within="cs_scene"))
                          + nop(*find(r"call cs_consider$", 1, within="cs_scene")))
    s["  rot (in scale)"] = nop(*find(r"call cs_rot$", within="cs_scale"))
    s["  poly (in faces)"] = nop(*find(r"call cs_poly$", within="cs_faces"))
    s["  clip (in faces)"] = nop(*find(r"call cs_fclip$", within="cs_faces"))
    s["  rows (in poly)"] = nop(*find(r"jmp \[cs_rowsproc\]$", within="cs_poly"))
    s["  edge (in poly)"] = nop(*find(r"call cs_edge$", within="cs_poly"))
    s["  seg (in edges)"] = nop(*find(r"call cs_seg$", within="cs_edge1"))
    s["panel"] = nop(*find(r"call cs_panel$", within="cs_render"))
    s["blit"] = nop(*find(r"call cs_blit$", within="cs_r_end"))
    # ...and the tick wait in cs_steps, patched out for the WHOLE run: a
    # frame faster than a tick would otherwise read as 55 ms, and every
    # "without" below it would read the same
    s["_wait"] = nop(*find(r"call OSAPI_FSX_WAIT$", within="cs_steps"))
    return s


def trace(m, lin, seg, base, off, render):
    """One frame, stopping at every cs_seg / cs_poly / cs_drawobj entry:
    the arguments (a segment's ends, a polygon's vertex count and rows) and
    the cycles from each stop to the next, which is what that call cost
    plus the caller's walk to the one after."""
    mp = dispapps._map("skies")
    segp, polyp, objp = mp["cs_seg"], mp["cs_poly"], mp["cs_drawobj"]
    marks = {mp[k]: k for k in ("cs_skyground", "cs_scene", "cs_drawpass",
                                "cs_panel", "cs_r_end", "cs_stackverts",
                                "cs_flatverts", "cs_projall", "cs_faces",
                                "cs_edges", "cs_boxlod", "cs_rect",
                                "cs_polyrows_herc", "cs_polyrows",
                                "cs_fclip", "cs_sidepass", "cs_cxing",
                                "cs_blit", "cs_steps", "cs_step", "cs_input",
                                "cs_r_begin", "cs_matrix", "cs_sound_step",
                                "cs_hzrows", "cs_edge", "cs_markrows", "cs_markacc")}
    m.bp_exec(lin + render)
    m.run()
    if m.wait_stop(20) is None:
        sys.exit("skiesperf: cs_render never ran")
    consp = mp["cs_consider"]
    m.bp_exec(lin + render, lin + segp, lin + polyp, lin + objp, lin + consp,
              *[lin + a for a in marks])
    c0 = m.status()["cycles"]
    n = 0
    print("  trace of one frame (a line's cycles are what the PREVIOUS line "
          "cost, up to this stop):")
    while True:
        m.run()
        if m.wait_stop(20) is None:
            sys.exit("skiesperf: the trace stalled")
        st = m.status()
        c1, ip = st["cycles"], st["ip"]
        r = m.regs()
        ax, bx, cx, dx = (r[k] & 0xFFFF for k in ("ax", "bx", "cx", "dx"))
        sg = lambda v: v - 0x10000 if v >= 0x8000 else v
        if ip == render:
            print("    ...%6d cycles to the next frame" % (c1 - c0))
            break
        if ip == consp:
            si = r["si"] & 0xFFFF
            name = int.from_bytes(m.readseg(seg, si + 14, 2), "little")
            nm = m.readseg(seg, name, 16).split(b"\0")[0].decode("ascii", "replace")
            rng = int.from_bytes(m.readseg(seg, si + 10, 2), "little")
            print("    %6d  consider %s (range %d)" % (c1 - c0, nm, rng))
        elif ip in marks:
            print("    %6d  [%s]" % (c1 - c0, marks[ip]))
        elif ip == objp:
            si = r["si"] & 0xFFFF
            ob = int.from_bytes(m.readseg(seg, si, 2), "little")
            name = int.from_bytes(m.readseg(seg, ob + 14, 2), "little")
            nm = m.readseg(seg, name, 16).split(b"\0")[0].decode("ascii", "replace")
            e = [sg(int.from_bytes(m.readseg(seg, si + 2 * i, 2), "little")) for i in range(1, 3)]
            print("    %6d  object %s at %d m along (reach %d)" % (c1 - c0, nm, e[1], e[0]))
        elif ip == segp:
            print("    %6d  seg (%d,%d)-(%d,%d)" % (c1 - c0, sg(ax), sg(bx), sg(cx), sg(dx)))
        else:
            si = r["si"] & 0xFFFF
            vs = [sg(int.from_bytes(m.readseg(seg, si + 4 * i + 2, 2), "little")) for i in range(cx)]
            print("    %6d  poly %d verts, rows %d..%d" % (c1 - c0, cx, min(vs), max(vs)))
        c0 = c1
        n += 1
        if n > 500:
            print("    ...more than 400 stops, giving up")
            break
    m.bp_exec()


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--c160", action="store_true",
                    help="price SPEC.md 88.15's 160x100x16 backend instead of"
                         " the one this display would take (a real CGA only)")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--scene", default="tower", choices=sorted(SCENES))
    ap.add_argument("--frames", type=int, default=12)
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument("--shot", help="write the pinned scene's picture here")
    ap.add_argument("--trace", action="store_true",
                    help="one frame's every cs_seg and cs_poly, with its "
                         "arguments and the cycles it took")
    a = ap.parse_args(argv)
    os.chdir(ROOT)
    lst = listing()
    S = sites(lst)
    os.unlink(lst)

    def off(n):
        return dispapps.bss_off("skies", n)
    render = dispapps._map("skies")["cs_render"]

    with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
        slot, seg, base = skiestest.open_game(m)
        lin = seg << 4

        def w(name):
            return int.from_bytes(m.readseg(seg, base + off(name), 2), "little")

        def poke(name, data, i=0):
            m.write(lin + base + off(name) + i, data)

        def frames(n):
            """n consecutive frames, ms each, by a breakpoint on cs_render."""
            m.bp_exec(lin + render)
            m.run()
            if m.wait_stop(20) is None:
                sys.exit("skiesperf: cs_render never ran")
            c0 = m.status()["cycles"]
            out = []
            for _ in range(n):
                m.run()
                if m.wait_stop(20) is None:
                    sys.exit("skiesperf: the frame never came")
                c1 = m.status()["cycles"]
                out.append((c1 - c0) / CPS * 1000.0)
                c0 = c1
            m.bp_exec()
            return out

        if a.c160:
            # SPEC.md 88.15's 16-colour text hack, which cs_adapter offers
            # only on a real CGA and only as the Mode row's second item: the
            # three bytes that row sets, so this instrument can price the
            # backend without driving the Settings page
            m.pause()
            poke("cs_modepref", b"\x01")
            poke("cs_want", b"\x04")           # CSB_C160
            poke("cs_fsxm", b"\x00")           # FSXM_TEXT80
            m.run()
        m.type_text("f")
        m.advance(frames=30)
        m.run()
        back = w("cs_back") & 0xFF
        print("  backend %d, view %dx%d, scene %s"
              % (back, w("cs_ww"), w("cs_wh"), a.scene))

        # --- pin the scene: the world paused, the aeroplane placed by poke ---
        sc = SCENES[a.scene]
        m.pause()
        if sc:
            x, y, z, hdg, pitch = sc[:5]
            roll = sc[5] if len(sc) > 5 else 0
            for name, v in (("cs_px", x), ("cs_py", y), ("cs_pz", z)):
                poke(name, ((v * 256) & 0xFFFFFFFF).to_bytes(4, "little"))
            poke("cs_hdg", ((hdg * 65536 // 360) & 0xFFFF).to_bytes(2, "little"))
            poke("cs_pitch", ((pitch * 65536 // 360) & 0xFFFF).to_bytes(2, "little"))
            poke("cs_roll", ((roll * 65536 // 360) & 0xFFFF).to_bytes(2, "little"))
            poke("cs_state", b"\x01")
        poke("cs_pause", b"\x01")           # the world stands still
        # ...and every object is looked at again: a poke is a teleport, and
        # the cull's skip counters (SPEC.md 88.5.2) were set where it was
        # THE WORLD IS THE PICKED LOCATION'S since SPEC.md 88.6.4, so the skips
        # to clear are the ones in the table its record names.
        ap = w("cs_airport")
        objs = int.from_bytes(m.read(lin + ap + 18, 2), "little")
        nobj = int.from_bytes(m.read(lin + ap + 20, 2), "little")
        for o in range(objs, objs + nobj * 20, 20):
            m.write(lin + o + 18, b"\x00\x00")
        m.run()
        frames(3)                           # a warm-up, discarded: the first
                                            # frame after a poke redraws the panel
        print("  objects in the frame: %d" % w("cs_nvisn"))
        if a.shot:
            m.pause()
            wd, hd, data = m.fbuf(0)
            os88marty.write_png_rgb(a.shot, wd, hd, data)
            m.run()

        def ms():
            v = frames(a.frames)
            if a.verbose:
                print("    frames: " + " ".join("%.1f" % x for x in v))
            return sum(v) / len(v)
        def patch(site, on):
            for i in range(0, len(site), 3):
                o, b, r = site[i], site[i + 1], site[i + 2]
                if on:
                    cur = m.read(lin + o, len(b))
                    if cur != b:
                        sys.exit("skiesperf: %04x holds %s, not %s - the image "
                                 "is not the tree" % (o, cur.hex(), b.hex()))
                    m.write(lin + o, r)
                else:
                    m.write(lin + o, b)
        if a.trace:
            trace(m, lin, seg, base, off, render)
        m.pause()
        patch(S.pop("_wait"), True)         # flat out, for the whole run
        m.run()
        base_ms = ms()
        print("  frame: %.2f ms (%.2f fps), mean of %d exact frames, the "
              "tick wait patched out" % (base_ms, 1000 / base_ms, a.frames))
        for name, site in S.items():
            m.pause()
            patch(site, True)
            m.run()
            t = ms()
            m.pause()
            patch(site, False)
            m.run()
            print("  without %-16s %7.2f ms  (%+7.2f, %4.1f%% of the frame)"
                  % (name, t, t - base_ms, 100 * (base_ms - t) / base_ms))


if __name__ == "__main__":
    main(sys.argv[1:])
