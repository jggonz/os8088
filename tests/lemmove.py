#!/usr/bin/env python3
"""LEMMINGS' kept level MOVES, and plays on (SPEC.md 116.6.2, NO-PINNING C4).

    make lemmingsdisk && python3 tests/lemmove.py         # both arms
    python3 tests/lemmove.py --machine os8088_xt_vga      # one of them
    python3 tests/lemmove.py --nomove                     # the A/B: no move

A level kept by 'f' holds up to ~330 KB of claims on the desktop (lemload.c's
seven lem_cl_* words). Until NO-PINNING C4 every byte of it was PINNED for as
long as the level was kept - a wall the heap could not close over. They are
MOVABLE now while the desktop holds them and PINNED again for the launch that
uses them, and the relocation proc (lemblit.inc's os88_onmove) range-
translates eighteen words: the seven claims, the C's segments DERIVED from
lem_cl_main (lem_anseg, lem_mkseg, lem_ptseg), lem_far_seg, and seven the
raster keeps between calls. The two arms are the two claim SETS: Hercules
(and CGA) keep a world and a shadow, VGA keeps the T planes instead.

THE RUN. MartyPC boots the shipped package off a SCRATCH copy of
build/lemmings360.img, launches Fun 2 like a user, plays fifteen seconds,
PAUSES (the game's own `lem_paused`: F11 is an AT key and these are 83-key
XTs), photographs the glass, SNAPSHOTS the machine, and keeps the level with
'f'. The desktop then FORCES a compaction from outside - the one thing a user
cannot do on cue - by writing `[mem_cpq]`, the posted-compaction request
`OSAPI_MEM_COMPACT` itself records (SPEC.md 66.4.3), so ui_task's step 0a
runs `mem_compact` with NOTHING held, at a rank (0xFC) that drops the trivial
caches the launch left below the level's claims - the icon store and a raise
cache, ~35 KB on both machines. That is a real hole under the claims and the
pass slides them into it. 'f' resumes, the game is unpaused and played to a
fixed iteration; then the SNAPSHOT is restored and the same level is played
to the same iteration with no suspend and no move - the CONTROL leg. One
emulator runs at a time: the original is paused while the restore plays.

It also prints what the moving pass COST (NO-PINNING-PLAN finding 7, gate
G1): `mem_compact`'s cycles from entry to its return, in ms at 4.77 MHz.

NINE ASSERTIONS, each about a different way the move can be wrong:

  1. PINNED WHILE IN USE: inside the bracket every claim's MC_RLOC is 0 -
     a local holds a claim's segment across a file read there (bucket C).
  2. MOVABLE ON THE DESKTOP: kept, every claim's MC_RLOC is non-zero.
  3. THEY MOVED, and the C followed: every lem_cl_* changed and each names
     a live claim of the same size in the kernel's own table.
  4. THE CONTENTS SURVIVED, every claim hashed at both addresses.
  5. THE DERIVED C WORDS FOLLOWED: lem_anseg, lem_mkseg and lem_ptseg keep
     their offset INSIDE lem_cl_main. A proc that fixed only the claims'
     own words passes 3 and 4 and fails here.
  6. THE RASTER'S WORDS FOLLOWED: lm_seg = lem_cl_mask, lo_seg = lem_cl_rec,
     lb_world, lb_shadow and lb_t = their claims - read BEFORE the resume,
     whose mode set re-writes four of them and would hide a miss.
  7. THE RESUMED LEVEL IS THE SAME PICTURE: paused, the glass after the
     resume is the glass before 'f', pixel for pixel - terrain composed out
     of the moved world/T claim, every lemming out of the moved LEMMAIN
     animation, so a stale word draws garbage here.
  8. PINNED AGAIN for the resumed bracket.
  9. IT PLAYS ON AS IT WOULD HAVE UNMOVED: at the same iteration the moved
     leg and the control leg agree on all 80 lemming records, the four
     counts, and the mask and the object map (the two claims the mechanics
     read AND write), and every lemming out is on the level.

SKIPS - exit 0, saying why - when build/lemmings360.img is not there: the
data files are fetched and never committed (SPEC.md 116.2).
"""
import argparse
import hashlib
import os
import shutil
import socket
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tools"), os.path.join(ROOT, "tests")]
import os88geom                                             # noqa: E402
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
import dispapps                                             # noqa: E402
import dispcp                                               # noqa: E402
import shot                                                 # noqa: E402
from lemplaymarty import lemmap                             # noqa: E402

S = os88sym.linear
HZ = 4772727.0
MC_SIZE, MEM_MAX = os88geom.MC_SIZE, os88geom.MEM_MAX
# THE SOURCE comes out of the run's tree (os88build.at, docs/plans/
# SOAK-PARALLEL.md 14.2), and the SCRATCH copy is handed to launch() as an
# ABSOLUTE path, which at() leaves alone: a `build/...` relative path is
# resolved against the frozen tree, where this row's own scratch file is not.
import os88build                                         # noqa: E402
IMG = os88build.at("build/lemmings360.img")
SCRATCH = os.path.join(ROOT, "build", "lemmove360.img")
CLAIMS = ("_lem_cl_mask", "_lem_cl_bank", "_lem_cl_main", "_lem_cl_world",
          "_lem_cl_shadow", "_lem_cl_rec", "_lem_cl_t")
DERIVED = ("_lem_anseg", "_lem_mkseg", "_lem_ptseg")    # inside lem_cl_main
RASTER = (("lm_seg", "_lem_cl_mask"), ("lo_seg", "_lem_cl_rec"),
          ("lb_world", "_lem_cl_world"), ("lb_shadow", "_lem_cl_shadow"),
          ("lb_t", "_lem_cl_t"))
LEM_STRIDE, LEM_MAXLEM = 32, 80                     # lemgame.c's lem_lm[]


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def mem_tab(m):
    """{base: (paragraphs, MC_RLOC)} of every live claim."""
    raw = m.read(S("mem_tab"), MEM_MAX * MC_SIZE)
    out = {}
    for i in range(MEM_MAX):
        o = i * MC_SIZE
        if u16(raw, o):
            out[u16(raw, o)] = (u16(raw, o + 2), u16(raw, o + 8))
    return out


def open_lemmings(m, mo):
    dispcp.open_drive(m, mo, S, os88marty.settle, "B")
    w = dispcp.win_list(m, S)
    wx, wy, _, _ = dispcp.win_rect(m, S, w[-1])
    dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "LEMMINGS")
    w = dispcp.win_list(m, S)
    wx, wy, _, _ = dispcp.win_rect(m, S, w[-1])
    dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "LEMMINGS.O88")
    os88marty.settle(m)


class Pkg(object):
    """LEMMINGS' statics on one machine, by the package's own map."""

    def __init__(self, m, sym, base):
        self.m, self.sym, self.base = m, sym, base

    def rd(self, name, n=2):
        return int.from_bytes(self.m.read(self.base + self.sym[name], n),
                              "little")

    def wr(self, name, v):
        self.m.write(self.base + self.sym[name], struct.pack("<H", v))

    def claims(self):
        return {n: self.rd(n) for n in CLAIMS if self.rd(n)}

    def run_to(self, it):
        """Run until the game is about to play iteration it + 1 - stopped at
        ovl_increment's entry with _lem_iter == it - and answer the mechanics
        there: the 80 lemming records, four counts, and the mask and the
        object map hashed. The breakpoint is off the module's own far vector,
        which the move re-stamps, so it is right after the carve moved too."""
        m = self.m
        v = self.rd("cc_ovm_ovl_increment", 4)
        m.bp_exec((v >> 16) * 16 + (v & 0xFFFF))
        for _ in range(4000):
            if self.rd("_lem_iter") >= it:
                break
            mark = m.go()
            if m.wait_stop(60, since=mark) != "breakpoint":
                break
        m.bp_exec()
        c = self.claims()
        return (self.rd("_lem_iter"),
                bytes(m.read(self.base + self.sym["_lem_lm"],
                             LEM_MAXLEM * LEM_STRIDE)),
                tuple(self.rd(n) for n in ("_lem_released", "_lem_out",
                                           "_lem_saved", "_lem_removed")),
                hashlib.md5(m.read(c["_lem_cl_mask"] * 16, 32000))
                .hexdigest(),
                hashlib.md5(m.read(c["_lem_cl_rec"] * 16, 15840))
                .hexdigest())


def sane(mech):
    """Every lemming released and not gone has its foot on the level."""
    recs, released = mech[1], mech[2][0]
    for k in range(min(released, LEM_MAXLEM)):
        x, y = struct.unpack_from("<hh", recs, k * LEM_STRIDE)
        if not recs[k * LEM_STRIDE + 6] & 0x02 and not (
                -16 <= x < 1680 and -16 <= y < 200):
            return False
    return released > 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", action="append", default=None,
                    help="a MartyPC machine (repeatable); default both arms, "
                    "os8088_5150_herc_gla (world + shadow) and os8088_xt_vga "
                    "(the T planes)")
    ap.add_argument("--png", default=None,
                    help="write the two paused pictures into this folder")
    ap.add_argument("--nomove", action="store_true",
                    help="the A/B: keep and resume with NO compaction")
    ap.add_argument("--iters", type=int, default=150,
                    help="game iterations played after the resume")
    a = ap.parse_args()
    if not os.path.exists(IMG):
        print("lemmove: SKIP - build/lemmings360.img is not there; it needs "
              "the fetched data (tools/getlemmings.py) and `make "
              "lemmingsdisk`")
        return 0
    sym = lemmap()
    failed = []
    for machine in a.machine or ("os8088_5150_herc_gla", "os8088_xt_vga"):
        print("lemmove: %s" % machine)
        bad = run(a, machine, sym)
        print("lemmove: %s %s" % (machine, "PASS" if not bad else
                                  "FAIL - assertion(s) %s" % bad))
        if bad:
            failed.append(machine)
    print("lemmove: %s" % ("PASS" if not failed else
                           "FAIL on %s" % ", ".join(failed)))
    return 1 if failed else 0


def run(a, machine, sym):
    shutil.copyfile(IMG, SCRATCH)
    bad = []

    def check(n, ok, text):
        print("  %d %-40s %s" % (n, text, "OK" if ok else "FAIL"))
        if not ok:
            bad.append(n)

    try:
        with os88marty.launch("build/os8088-360.img",
                              apps=SCRATCH,
                              machine=machine, label="lemmove") as m:
            os88marty.no_saver(m)
            mo = os88mouse.Mouse(marty=m)
            open_lemmings(m, mo)
            pk = dispapps.pkg_seg(m, 0)
            if not pk:
                print("lemmove: FAIL - LEMMINGS did not launch")
                return ["launch"]
            P = Pkg(m, sym, pk[1] * 16)

            def glass():
                return m.fbuf()[2]

            def save(name, pix):
                if a.png:
                    w, h, _ = m.fbuf()
                    os.makedirs(a.png, exist_ok=True)
                    shot.png(os.path.join(a.png, name), w, h, pix)

            # --- launch Fun 2 (the 360KB disk carries it, Fun 1 it does not:
            # tests/lemplaymarty.py) and play until the lemmings are out ----
            P.wr("_lem_sel", 1)
            m.key("Enter")
            os88marty.until(m, lambda _: P.rd("_lem_claimed"),
                            "the level's claims", poll=1)
            m.advance(cycles=int(20 * HZ))      # the preview, drawn
            m.run()
            m.key("Enter")
            it0 = P.rd("_lem_iter")
            os88marty.until(m, lambda _: P.rd("_lem_iter") != it0,
                            "the level to start", poll=1)
            m.advance(cycles=int(15 * HZ))      # out of the trap door
            m.run()
            tab = mem_tab(m)
            c0 = P.claims()
            check(1, bool(c0) and all(tab.get(s, (0, 1))[1] == 0
                                      for s in c0.values()),
                  "pinned while in use (%d claims)" % len(c0))

            # --- paused, photographed, snapshotted, kept ---------------------
            P.wr("_lem_paused", 1)
            m.advance(cycles=int(3 * HZ))       # the pass that saw it
            m.run()
            g0 = glass()
            itp = P.rd("_lem_iter")
            m.pause()
            snap = m.snapshot()["id"]           # the control leg's start
            m.run()
            m.key("KeyF")
            os88marty.until(m, lambda _: P.rd("_lem_suspended"),
                            "the level to be kept", poll=1)
            os88marty.settle(m)
            tab = mem_tab(m)
            c0 = P.claims()
            check(2, all(tab.get(s, (0, 0))[1] != 0 for s in c0.values()),
                  "movable on the desktop")
            size = {n: tab[s][0] for n, s in c0.items()}
            h0 = {n: hashlib.md5(m.read(s * 16, size[n] * 16)).hexdigest()
                  for n, s in c0.items()}
            off0 = {d: P.rd(d) - c0["_lem_cl_main"] for d in DERIVED
                    if P.rd(d)}
            print("  kept:  %s" % " ".join("%s=%04x" % (n[8:], s)
                                          for n, s in c0.items()))

            # --- THE FORCED COMPACTION, and what it costs (NO-PINNING G1) --
            lw = [x for x in os88geom.windows(m, S)
                  if x.visible and x.title.startswith("Lemmings")]
            cyc = None
            if not a.nomove:
                m.bp_exec("mem_compact")
                m.write(S("mem_cpq_lvl"), bytes([0xFC]))    # drop TRIV caches
                m.write(S("mem_cpq"),
                        struct.pack("<H", os88geom.winptr(m, lw[0].i, S)))
            if not a.nomove and m.wait_stop(120) == "breakpoint":
                rg = m.regs()
                c_in = int(m.status().get("cycles", 0))
                ret = u16(m.read((rg["ss"] << 4) + rg["sp"], 2))
                m.bp_exec(rg["cs"] * 16 + ret)
                mark = m.go()
                if m.wait_stop(120, since=mark) == "breakpoint":
                    cyc = int(m.status().get("cycles", 0)) - c_in
            m.bp_exec()
            m.run()
            os88marty.until(m, lambda _: u16(m.read(S("mem_cpq"), 2)) == 0,
                            "the posted compaction", poll=0.5)
            os88marty.settle(m)
            tab = mem_tab(m)
            c1 = P.claims()
            print("  moved: %s" % " ".join("%s=%04x" % (n[8:], s)
                                          for n, s in c1.items()))
            moved = sum(size[n] for n in c0 if c1.get(n) != c0[n]) * 16
            if cyc is not None:
                print("  mem_compact: %d cycles = %.1f ms at 4.77 MHz, "
                      "LEMMINGS' %d KB of it moved"
                      % (cyc, cyc * 1000.0 / HZ, moved // 1024))
            else:
                print("  mem_compact: NOT TIMED (no stop at its entry)")
            check(3, set(c1) == set(c0)
                  and all(c1[n] != c0[n] for n in c0)
                  and all(tab.get(c1[n], (0,))[0] == size[n] for n in c0),
                  "moved, and lem_cl_* followed")
            check(4, all(hashlib.md5(m.read(c1[n] * 16, size[n] * 16))
                         .hexdigest() == h0[n] for n in c0 if n in c1),
                  "contents survived")
            off1 = {d: P.rd(d) - c1.get("_lem_cl_main", 0) for d in off0}
            check(5, bool(off0) and off1 == off0,
                  "derived C words followed (%d)" % len(off0))
            if off1 != off0:
                print("      offsets inside LEMMAIN %r, now %r"
                      % (off0, off1))
            ras = {r: (P.rd(r), c1.get(c, 0)) for r, c in RASTER}
            check(6, all(v[0] == v[1] for v in ras.values()),
                  "raster words followed")
            if not all(v[0] == v[1] for v in ras.values()):
                print("      %r" % ras)

            # --- resume: the same picture, pinned again ----------------------
            m.key("KeyF")
            os88marty.until(m, lambda _: not P.rd("_lem_suspended")
                            and P.rd("_lem_lvl_ok"), "the resume", poll=1)
            # THE RESUME RE-COMPOSES THE WHOLE FRAME before its one present
            # (lemdraw.c ovl_frame_first): ~5.5 guest seconds of a black
            # glass on the 5150. Wait for a lit picture that holds still
            g1, prev = b"", None
            for _ in range(120):
                m.advance(cycles=int(0.5 * HZ))
                m.run()
                g = glass()
                if g == prev and any(g[i] for i in range(0, len(g), 3)):
                    g1 = g
                    break
                prev = g
            save("lemmove-before.png", g0)
            save("lemmove-after.png", g1)
            diff = sum(1 for i in range(0, min(len(g0), len(g1)), 3)
                       if g0[i:i + 3] != g1[i:i + 3])
            check(7, len(g0) == len(g1) and diff == 0,
                  "resumed picture identical (%d px)" % diff)
            tab = mem_tab(m)
            check(8, all(tab.get(s, (0, 1))[1] == 0
                         for s in P.claims().values()),
                  "pinned again for the bracket")

            # --- ...and it PLAYS ON exactly as it would have unmoved ---------
            P.wr("_lem_paused", 0)
            mech_a = P.run_to(itp + a.iters)
            m.pause()
            with socket.socket() as sk:
                sk.bind(("127.0.0.1", 0))
                port = sk.getsockname()[1]
            m2 = m.restore(snap, port)
            try:
                if m2.status().get("state") != "running":
                    m2.run()
                P2 = Pkg(m2, sym, P.base)
                P2.wr("_lem_paused", 0)
                mech_b = P2.run_to(itp + a.iters)
            finally:
                try:
                    m2.cmd(cmd="quit")
                except os88marty.MartyError:
                    pass
            same = mech_a == mech_b
            print("  played %d -> %d; released %d, out %d, saved %d; the "
                  "control leg (no suspend, no move) %s"
                  % (itp, mech_a[0], mech_a[2][0], mech_a[2][1],
                     mech_a[2][2], "AGREES" if same else
                     "DIFFERS: %r vs %r" % ((mech_a[0],) + mech_a[2:],
                                             (mech_b[0],) + mech_b[2:])))
            check(9, mech_a[0] == itp + a.iters and same and sane(mech_a),
                  "plays on as it would have unmoved")
    finally:
        try:
            os.unlink(SCRATCH)
        except OSError:
            pass
    return bad


if __name__ == "__main__":
    sys.exit(main())
