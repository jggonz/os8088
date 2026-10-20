#!/usr/bin/env python3
"""PIXELSTEIN 3D's status bar is CHANGE-ONLY (SPEC.md 97.13; wave 4).

    python3 tests/pxshud.py [--machine os8088_5150_cga_gla]

The bar - floor, score, lives, the face, health, ammo, the keys, the weapon
- keeps every field's value as last drawn ON EACH PAGE (px_hudv) and a
frame redraws a field only where the value differs. What that promises,
read off the package's own counter of field rewrites (px_hudn) and off the
glass:

  (a) A QUIET SECOND REWRITES NOTHING - in a window, the turn key held for
      a second so frames ARE drawn (px_frames climbs), and px_hudn does not
      move: the bar is not a per-frame cost (tests/pixelstein.py's numbers
      are the other half of that claim);
  (b) the window's bar is in the shadow's rows 80..103 at bytes 20..59 -
      320 of the band's 512 dots - and nothing of it outside them;
  (c) ONE CHANGE IS ONE FIELD: the rounds poked 8 -> 5 and exactly one
      field is rewritten, and the shadow's bar differs only inside the
      ammo field's two cells;
  (d) the same quiet second in the BRACKET rewrites nothing, and the bar
      ON THE GLASS is the shadow's byte for byte (CGA 320x200: the device
      rows 108..131 through px_hudoff, the present's rectangle copy);
  (e) health poked 100 -> 55 rewrites TWO fields - the digits and the face,
      whose frame follows the health (97.4) - and the glass follows again;
  (f) ON MODE X (--machine os8088_xt_vga) THE BAR IS PER PAGE: the same
      change rewrites each field ONCE ON EACH PAGE (px_hudn +2 a field),
      the two pages' px_hudv agree afterwards, and the two pages' bar rows
      read the same bytes (plane 0);
  (g) A MESSAGE OVER THE SAME MESSAGE IS A REWRITE: V twice inside the
      message's two seconds posts SIZE over SIZE, and the label row is
      rewritten both times (the field's value carries the post's serial);
  (h) AFTER THE BRACKET, A QUIET SECOND IN PLAY DRAWS NO FRAME: px_cardd is
      0 (a card is owed only in a card state) and px_frames does not move
      - the premise every "a still player costs nothing" number rests on;
  (i) ON MODE X A SHOT LEAVES THE WEAPON ON BOTH PAGES (wave 5, the defect
      wave 4's verifier found - present since wave 3): the pistol fired
      once, then forty ticks with the eye still, and the weapon's rect
      (plane 0 of rows 56..79 over its sixteen byte columns) is BYTE FOR
      BYTE what it was before the shot ON EACH PAGE; at rest the two pages
      agree and the 154 cells the pistol's frame 0 writes are the reference
      renderer's (tools/pxssim.py draw_weapon). The count printed is the
      rect's non-floor bytes, walls included: the unfixed code reads page 0
      259 -> 219 on the XT-VGA, the fixed 259 -> 259 on both: two defects. px_weapon_touch's
      ground arm re-lays a column whose wall ends above row 56 WITHOUT
      marking px_cwrote, and px_weapon_draw drew only over a column it found
      marked - so a frame change over a hall floor erased the weapon and
      drew nothing (the verifier's "gone until the eye moves"); and a shot's
      three frames land on the two pages in turn, so the page not drawn
      last kept the fire or recoil frame for the next flip to show. Fixed:
      the check's erase owes the draw (px_wtouch), and a draw that leaves
      the other page on another frame owes one frame more (97.6).
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))     # LAST, so it wins (pxslib)
import os88marty                                                # noqa: E402
import pxslib                                                   # noqa: E402

FAIL = []
HUDROW0, HUDROWS, STRIDE = 80, 24, 80
PXF_N = 10


def check(ok, what):
    print("   %-70s %s" % (what, "ok" if ok else "FAIL"))
    if not ok:
        FAIL.append(what)


def ticks(g, n, limit=120.0):
    t0 = g.ticks()
    os88marty.until(g.m, lambda mm: (g.ticks() - t0) & 0xFFFFFF >= n,
                    "%d ticks" % n, poll=0.1, limit=limit)


def shadow_bar(g):
    seg = g.word("px_shseg")
    return g.m.read((seg << 4) + HUDROW0 * STRIDE, HUDROWS * STRIDE)


def glass_bar_cga(g):
    out = bytearray()
    for r in range(HUDROWS):
        y = 28 + HUDROW0 + r
        out += g.m.read(0xB8000 + (y & 1) * 0x2000 + (y >> 1) * 80, STRIDE)
    return bytes(out)


def modex_bar(g, page):
    seg = pxslib.u16(g.bytes_("px_fsi", 2))
    return g.m.read((seg << 4) + page * 19200 + (48 + HUDROW0) * STRIDE, HUDROWS * STRIDE)


def modex_weapon(g, page):
    """(the weapon's lit bytes, the rect) on one Mode X page: plane 0 of view
    rows 56..79 over the weapon's sixteen byte columns at the band's middle
    (px_weapon_cols), counting the bytes that are not the floor's DAC entry
    (px_inkf = 0x0808 on Mode X) - with the eye still and nothing in view
    there, that is the weapon."""
    seg = pxslib.u16(g.bytes_("px_fsi", 2))
    size, x0 = g.byte("px_size"), g.byte("px_x0")
    c0 = x0 + size // 2 - 8
    out = bytearray()
    for r in range(56, 80):
        out += g.m.read((seg << 4) + page * 19200 + (48 + r) * STRIDE + c0, 16)
    return sum(1 for b in out if b != 0x08), bytes(out)


def rest_weapon(g):
    """{offset in modex_weapon's rect: byte} for the cells the pistol's REST
    frame writes, off the reference renderer (tools/pxssim.py draw_weapon,
    Mode X, the rung and resolution in force). A cell is 'written' when two
    grounds give the same byte there."""
    import pxssim                                           # noqa: E402
    low = bool(g.byte("px_lowres"))
    rung = {0: "wire", 1: "flat", 2: "tex"}[g.byte("px_rung")]
    size = g.byte("px_size")
    cols = size // 2 if low else size
    a = pxssim.draw_weapon(bytearray([0x08]) * 6400, "modex", cols, low, rung)
    b = pxssim.draw_weapon(bytearray([0x77]) * 6400, "modex", cols, low, rung)
    bpc, x0 = pxssim.geometry("modex", cols, low)
    c0 = x0 + size // 2 - 8
    out = {}
    for r in range(56, 80):
        for k in range(16):
            o = r * 80 + c0 + k
            if a[o] == b[o]:
                out[(r - 56) * 16 + k] = a[o]
    return out


def quiet(g, what):
    """Hold the turn key for a second of guest time: frames drawn, the bar
    untouched."""
    n0, f0 = g.word("px_hudn"), g.word("px_frames")
    g.m.key("ArrowRight", down=True, up=False)
    ticks(g, 18)
    g.m.key("ArrowRight", down=False, up=True)
    ticks(g, 3)
    n1, f1 = g.word("px_hudn"), g.word("px_frames")
    print("   %s: %d frames drawn over a second, %d bar field(s) rewritten"
          % (what, (f1 - f0) & 0xFFFF, (n1 - n0) & 0xFFFF))
    check((f1 - f0) & 0xFFFF >= 3, "(%s) frames WERE drawn in the quiet second (%d)"
          % (what, (f1 - f0) & 0xFFFF))
    check(n1 == n0, "(%s) and NO bar field was rewritten (px_hudn %d -> %d)" % (what, n0, n1))


def poke_and_count(g, name, value, frames=2):
    g.m.pause()
    n0 = g.word("px_hudn")
    g.poke_byte(name, value)
    g.m.run()
    ticks(g, 6 * frames)
    return (g.word("px_hudn") - n0) & 0xFFFF


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/games360.img")
    a = ap.parse_args()
    os.chdir(ROOT)
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
        g = pxslib.open_game(m)
        g.sim(False)
        g.god(True)
        g.scene("a")
        g.wait_frames(1)
        ticks(g, 6)
        # --- (a) a quiet second in the window ---------------------------------
        quiet(g, "window")
        # --- (b) where the window's bar is -------------------------------------
        bar = shadow_bar(g)
        inside = sum(1 for r in range(HUDROWS) for x in range(20, 60) if bar[r * STRIDE + x])
        outside = sum(1 for r in range(HUDROWS) for x in list(range(0, 20)) + list(range(60, 80))
                      if bar[r * STRIDE + x])
        check(inside > 100 and outside == 0, "the window's bar is drawn at bytes 20..59 of shadow "
              "rows 80..103 and nowhere else (%d lit bytes in, %d out)" % (inside, outside))
        # --- (c) one change, one field -----------------------------------------
        before = shadow_bar(g)
        d = poke_and_count(g, "px_ammo", 5)
        after = shadow_bar(g)
        cols = sorted(set(i % STRIDE for i in range(len(after)) if after[i] != before[i]))
        rows = sorted(set(i // STRIDE for i in range(len(after)) if after[i] != before[i]))
        print("   the rounds 8 -> 5: %d field(s) rewritten, bytes %s changed on rows %s..%s"
              % (d, cols, rows[:1], rows[-1:]))
        check(d == 1, "ONE change is ONE field rewritten (%d)" % d)
        check(cols and set(cols) <= {20 + 28, 20 + 29},
              "...and the bar changed inside the ammo field's two cells alone (bytes %s)" % cols)
        # --- (d) the bracket ------------------------------------------------------
        g.enter_fsx()
        g.pin(rung="tex", lowres=True, size=64)
        g.wait_frames(1, limit=180.0)       # (the sets built behind the black)
        g.scene("a")
        g.wait_frames(1)
        ticks(g, 6)
        back = pxslib.PXB.get(g.byte("px_back"))
        print("   the bracket's backend: %s" % back)
        quiet(g, "bracket")
        if back == "cga4":
            sh, gl = shadow_bar(g), glass_bar_cga(g)
            diff = sum(1 for x, y in zip(sh, gl) if x != y)
            check(diff == 0 and any(sh), "the bar ON THE GLASS is the shadow's, byte for byte "
                  "(%d of %d differ)" % (diff, len(sh)))
        # --- (e) health: two fields, and the glass follows -------------------------
        d = poke_and_count(g, "px_health", 55, frames=3)
        if back == "modex":
            # --- (f) per page: each field once on EACH page ----------------------
            hv = g.bytes_("px_hudv", 2 * PXF_N * 2)
            p0, p1 = hv[:PXF_N * 2], hv[PXF_N * 2:]
            print("   Mode X: health 100 -> 55: %d field rewrite(s) over both pages" % d)
            check(d == 4, "per page: the digits and the face rewritten ONCE ON EACH PAGE (%d)" % d)
            check(p0 == p1, "...and the two pages' px_hudv agree afterwards")
            b0, b1 = modex_bar(g, 0), modex_bar(g, 1)
            diff = sum(1 for x, y in zip(b0, b1) if x != y)
            check(diff == 0 and any(b0), "...and the two pages' bar rows read the same bytes "
                  "(%d differ)" % diff)
        else:
            print("   health 100 -> 55: %d field(s) rewritten" % d)
            check(d == 2, "health 100 -> 55 rewrites TWO fields: the digits and the face (%d)" % d)
            if back == "cga4":
                sh, gl = shadow_bar(g), glass_bar_cga(g)
                diff = sum(1 for x, y in zip(sh, gl) if x != y)
                check(diff == 0, "...and the glass follows the shadow (%d differ)" % diff)
        # --- (g) SIZE over SIZE ----------------------------------------------------
        if back != "c160":                  # (C160's bar has no label row)
            g.m.type_text("v")
            ticks(g, 4)
            n1 = g.word("px_hudn")
            g.m.type_text("v")
            ticks(g, 4)
            n2 = g.word("px_hudn")
            print("   V, V: %d field rewrite(s) on the second press (px_hmsg %d)"
                  % ((n2 - n1) & 0xFFFF, g.byte("px_hmsg")))
            check((n2 - n1) & 0xFFFF >= 1 and g.byte("px_hmsg") == 3,
                  "(g) SIZE posted over SIZE rewrites the label row again")
            g.pin(rung="tex", lowres=True, size=64)
            g.wait_frames(1)
        if back == "modex":
            # --- (i) a shot leaves the weapon on BOTH pages --------------------
            g.m.pause()
            g.poke_byte("px_nact", 0)       # nothing walks into the rect
            g.poke_byte("px_ammo", 20)
            g.poke_byte("px_weapon", 1)
            g.m.run()
            g.sim(True)                     # the weapon's frames are the sim's
            g.force()                       # a whole frame on one page...
            g.wait_frames(1)
            ticks(g, 6)
            g.force()                       # ...and on the OTHER: both show
            g.wait_frames(1)                # the weapon at rest
            ticks(g, 6)
            before = [modex_weapon(g, p) for p in (0, 1)]
            g.m.key("ControlLeft", down=True, up=False)
            ticks(g, 3)
            g.m.key("ControlLeft", down=False, up=True)
            ticks(g, 40)
            after = [modex_weapon(g, p) for p in (0, 1)]
            print("   Mode X, a shot then 40 still ticks: the weapon's lit bytes page 0 "
                  "%d -> %d, page 1 %d -> %d (ammo %d)" % (before[0][0], after[0][0],
                                                        before[1][0], after[1][0],
                                                        g.byte("px_ammo")))
            check(g.byte("px_ammo") == 19, "(i) the shot was fired (ammo 20 -> %d)"
                  % g.byte("px_ammo"))
            check(before[0][0] > 50 and before[1][0] > 50,
                  "(i) the weapon stands on both pages before the shot")
            # BYTES, not counts (review, wave 5): the rest frame is the
            # REFERENCE RENDERER's - tools/pxssim.py's draw_weapon, frame 0 of
            # the pistol at this rung and resolution, over the cells it writes
            # - on both pages, the two pages agree, and a shot then forty
            # still ticks leaves each page's bytes exactly as they were
            want = rest_weapon(g)
            for p in (0, 1):
                off = [(i, before[p][1][i], v) for i, v in want.items()
                       if before[p][1][i] != v]
                print("   page %d at rest: %d weapon cell(s), %d off the reference%s"
                      % (p, len(want), len(off), (" - first %s" % off[:4]) if off else ""))
                check(not off, "(i) page %d's rest frame is tools/pxssim.py's pistol frame 0"
                      % p)
            check(before[0][1] == before[1][1],
                  "(i) ...and the two pages agree at rest, byte for byte")
            check(after[0][0] == before[0][0] and after[1][0] == before[1][0],
                  "(i) ...and on BOTH pages after it, unchanged, the eye still (counts)")
            check(after[0][1] == before[0][1] and after[1][1] == before[1][1],
                  "(i) ...byte for byte")
            g.sim(False)
        g.leave_fsx()
        # --- (h) back in the window: a quiet second draws nothing ------------------
        os88marty.quiesce(g.m, lambda: g.word("px_frames"), guest=0.5,
                          what="the window's own settling frames")   # were
        f0 = g.word("px_frames")                                     # 60 ticks
        ticks(g, 18)
        f1 = g.word("px_frames")
        print("   after the bracket, PLAY, a quiet second: %d frame(s), px_cardd %d"
              % ((f1 - f0) & 0xFFFF, g.byte("px_cardd")))
        check(f1 == f0 and g.byte("px_cardd") == 0 and g.gstate() == pxslib.PXST["play"],
              "(h) back from the bracket, a still player in PLAY costs no frame")
    if FAIL:
        print("pxshud: FAIL (%d)" % len(FAIL))
        for f in FAIL:
            print("  -", f)
        return 1
    print("pxshud: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
