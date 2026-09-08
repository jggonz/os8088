#!/usr/bin/env python3
"""TANK ATTACK's frame, priced EXACTLY on MartyPC (SPEC.md 85.3.4).

    python3 tests/tankperf.py [--machine os8088_5150_herc_gla] [--scene fixed|heavy|live]

AN INSTRUMENT, NOT A GATE - it is registered as such in tests/unit/t_registry.py
and asserts nothing. It exists because the two obvious ways of pricing this
frame were both wrong when the round behind SPEC.md 85.3.4 opened:

- a sampled CS:IP profile read the line walk at nearly TWICE its exact share,
  and its first run was profiling a GAME OVER screen parked in fsx_wait,
  because a stationary player is dead inside 28 s; and
- counting frames over a few guest seconds is quantised to ONE FRAME, which
  at 3-6 fps is a 6-20% error on every reading and made three stages read
  exactly zero.

So the frame is timed by a breakpoint on tk_render, twelve consecutive
frames, cycle-exact; each drawing stage is then priced by patching its call
out (NOPs for a `call`, a `ret` over a `jmp`) and reading the delta - the
sites resolved out of a fresh listing by instruction text, never by
remembered offset; the player is kept alive by poking every tank's cooldown;
and the SCENE is pinned, the world paused and the pieces and the enemy placed
by poke, so two builds price the same frame.

    fixed   twelve pieces seeded over the torus, the enemy at 3,200 out
    heavy   eight pieces inside 2,300 units, the enemy at 900: the cluster
            SPEC.md 85.6.6.3 exists to stop being dealt
    live    what tk_newgame deals, eight fresh games, still and turning
"""
import argparse
import os
import random
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88marty                                            # noqa: E402
import dispapps                                             # noqa: E402
import tank as tanktest                                     # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CPS = 4772727                           # the 4.77 MHz clock: cycles a second
TK_NSTAT, TK_NOBJ = 12, 18
HEAVY = [                               # (type, x, z) about a player at the
    (1, -600, 500), (3, 400, 650), (2, -200, 900), (1, 700, 1000),    # origin
    (3, -700, 1400), (1, 200, 1500), (2, -450, 2000), (1, 650, 2300),  # facing
    (3, 0, 3200), (1, -900, 3800), (2, 900, 4200), (1, 300, 5200)]    # +z


def listing():
    """Assemble the tree's Tank with a listing, into a temp file."""
    fd, lst = tempfile.mkstemp(prefix="tankperf_", suffix=".lst")
    os.close(fd)
    r = subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", "apps/",
                        "-I", "apps/tank/", "-o", os.devnull, "-l", lst,
                        "apps/tank/tank.asm"], capture_output=True, text=True)
    if r.returncode:
        sys.exit("tankperf: the tree does not assemble:\n" + r.stderr[:400])
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
            sys.exit("tankperf: no site matches %r in the listing" % pat)
        return hits[nth]

    def nop(a, b):
        return (a, b, b"\x90" * len(b))

    def ret(a, b):
        return (a, b, b"\xc3" + b"\x90" * (len(b) - 1))
    s = {}
    s["walk"] = (ret(*find(r"jmp \[tk_lsh\]")) + ret(*find(r"jmp \[tk_lst\]"))
                 + ret(*find(r"jmp \[tk_lvt\]")))
    s["hrun"] = ret(*find(r"jmp \[tk_hrunproc\]"))
    s["blit"] = nop(*find(r"call tk_blit$", within="tk_r_end"))
    s["clearspans"] = nop(*find(r"call tk_clearspans"))
    s["ridge"] = nop(*find(r"call tk_ridge(_tm)?$"))    # dynamic or 85.3.8's
    s["drawtype x3"] = (nop(*find(r"call tk_drawtype", 0)) + nop(*find(r"call tk_drawtype", 1))
                        + nop(*find(r"call tk_drawtype", 2)))
    s["drawmovers"] = nop(*find(r"call tk_drawmovers"))
    s["hud"] = nop(*find(r"call tk_hud$"))
    return s


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--scene", default="fixed", choices=("fixed", "heavy", "live"))
    ap.add_argument("--frames", type=int, default=12)
    ap.add_argument("--games", type=int, default=8)
    ap.add_argument("--verbose", action="store_true", help="print every frame")
    ap.add_argument("--turn-every", type=int, default=0,
                    help="pinned scenes: turn the heading by TK_TURN every N frames "
                         "(1 = every frame, so the ridge never settles; 2 = the "
                         "alternation SPEC.md 85.3.8 calls the case that cannot win)")
    a = ap.parse_args(argv)
    os.chdir(ROOT)
    lst = listing()
    S = sites(lst)
    os.unlink(lst)

    def off(n):
        return dispapps.bss_off("tank", n)
    render = dispapps._map("tank")["tk_render"]

    with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
        slot, seg, base = tanktest.open_game(m)
        lin = seg << 4

        def w(name, i=0):
            return int.from_bytes(m.readseg(seg, base + off(name) + i, 2), "little")

        def poke(name, data, i=0):
            m.write(lin + base + off(name) + i, data)

        def alive():
            poke("tk_lives", b"\x04")
            poke("tk_dead", b"\x00")
            for k in range(TK_NSTAT, TK_NOBJ):
                poke("tk_ocool", b"\xff", k)

        turn = [0]

        def frames(n):
            """n consecutive frames, ms each, by a breakpoint on tk_render."""
            alive()
            m.bp_exec(lin + render)
            m.run()
            if m.wait_stop(20) is None:
                sys.exit("tankperf: tk_render never ran")
            c0 = m.status()["cycles"]
            out = []
            for _ in range(n):
                if a.turn_every:
                    turn[0] += 1
                    if turn[0] % a.turn_every == 0:
                        pa = m.readseg(seg, base + off("tk_pa"), 1)[0]
                        poke("tk_pa", bytes([(pa + 2) & 0xFF]))
                m.run()
                if m.wait_stop(20) is None:
                    sys.exit("tankperf: the frame never came")
                c1 = m.status()["cycles"]
                out.append((c1 - c0) / CPS * 1000.0)
                c0 = c1
            m.bp_exec()
            return out

        m.type_text("f")
        m.advance(frames=30)
        back = w("tk_back") & 0xFF
        print("  backend %d, viewport %dx%d, scene %s%s"
              % (back, w("tk_vw"), w("tk_vh"), a.scene,
                 (", turning every %d frames" % a.turn_every) if a.turn_every else ""))

        if a.scene == "live":
            allst, alltn = [], []
            for g in range(a.games):
                m.run()
                m.type_text("n")
                m.advance(cycles=CPS)
                st = frames(10)
                m.run()
                m.key("KeyD", down=True, up=False)
                m.advance(cycles=CPS // 2)
                tn = frames(10)
                m.run()
                m.key("KeyD", down=False, up=True)
                m.advance(cycles=CPS // 5)
                allst += st
                alltn += tn
                print("  game %d: still %5.1f ms (%4.1f fps)  turning %5.1f ms (%4.1f fps, worst %5.1f)"
                      % (g + 1, sum(st) / len(st), 1000 * len(st) / sum(st),
                         sum(tn) / len(tn), 1000 * len(tn) / sum(tn), max(tn)))
            for lab, v in (("still", allst), ("turning", alltn)):
                v2 = sorted(v)
                print("  %s over %d games: mean %.1f ms (%.2f fps), 90th %.1f, worst %.1f"
                      % (lab, a.games, sum(v) / len(v), 1000 * len(v) / sum(v),
                         v2[int(0.9 * (len(v2) - 1))], v2[-1]))
            return

        # --- a pinned scene: the world paused, every piece placed by poke ---
        if a.scene == "heavy":
            layout = HEAVY
        else:
            r = random.Random(7)
            layout = [(1 + (i % 3), r.randint(-4000, 4000), r.randint(-4000, 4000))
                      for i in range(12)]
        poke("tk_px", b"\x00\x00")
        poke("tk_pz", b"\x00\x00")
        poke("tk_pa", b"\x00")
        for i, (t, x, z) in enumerate(layout):
            poke("tk_otype", bytes([t]), i)
            poke("tk_ox", (x & 8191).to_bytes(2, "little"), i * 2)
            poke("tk_oz", (z & 8191).to_bytes(2, "little"), i * 2)
        for i in range(TK_NSTAT, TK_NOBJ):
            poke("tk_otype", b"\x00", i)
        poke("tk_otype", b"\x04", TK_NSTAT)         # one enemy, placed
        poke("tk_ox", (150).to_bytes(2, "little"), TK_NSTAT * 2)
        poke("tk_oz", (900 if a.scene == "heavy" else 3200).to_bytes(2, "little"), TK_NSTAT * 2)
        poke("tk_oa", b"\x80", TK_NSTAT)
        poke("tk_pause", b"\x01")                   # the world stands still

        def ms():
            v = frames(a.frames)
            if a.verbose:
                print("    frames: " + " ".join("%.1f" % x for x in v))
            return sum(v) / len(v)
        frames(3)                               # A WARM-UP, DISCARDED. Moving the
                                                # pieces by poke changes template
                                                # keys, and the first frame after
                                                # it redraws the panel: 282 ms
                                                # once, in a run whose steady
                                                # frame is 104, and it inflated
                                                # a twelve-frame mean by 17 ms
        base_ms = ms()
        print("  frame: %.2f ms (%.2f fps), mean of %d exact frames" % (base_ms, 1000 / base_ms, a.frames))

        def patch(site, on):
            for i in range(0, len(site), 3):
                o, b, r = site[i], site[i + 1], site[i + 2]
                if on:
                    cur = m.read(lin + o, len(b))
                    if cur != b:
                        sys.exit("tankperf: %04x holds %s, not %s - the image is not the tree"
                                 % (o, cur.hex(), b.hex()))
                    m.write(lin + o, r)
                else:
                    m.write(lin + o, b)
        for name, site in S.items():
            patch(site, True)
            t = ms()
            patch(site, False)
            print("  without %-12s %7.2f ms  (%+7.2f, %4.1f%% of the frame)"
                  % (name, t, t - base_ms, 100 * (base_ms - t) / base_ms))
        for name in ("walk", "hrun"):
            patch(S[name], True)
        t = ms()
        patch(S["blit"], True)
        patch(S["clearspans"], True)
        t2 = ms()
        for name in S:
            patch(S[name], False)
        print("  no pixel drawn: %.2f ms; and no clear or blit: %.2f ms - the floor"
              % (t, t2))


if __name__ == "__main__":
    main(sys.argv[1:])
