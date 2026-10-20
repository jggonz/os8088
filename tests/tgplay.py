#!/usr/bin/env python3
"""TameGram, played the same way twice: what a tick costs, and what it draws.

    make && python3 tests/tgplay.py                       # the soak row
    python3 tests/tgplay.py --card herc --ticks 1500
    python3 tests/tgplay.py --save base.json              # record a run...
    python3 tests/tgplay.py --ref base.json               # ...and hold another
                                                          # build to it
    python3 tests/tgplay.py --src old.asm --games old/games360.img

TameGram (SPEC.md 49) had no row at all until apps size pass 1 set 6, which
rewrote most of it; this is the row that pass was held to.

Everything the game does happens on one tick of `tg_worker`'s loop -
`tg_update` then `tg_frame`, both inside ONE gfx lock hold - so a tick is the
unit worth pricing, and this brackets every one: a breakpoint on the worker's
`call tg_update`, its `call tg_frame` and its `call OSAPI_GFX_UNLOCK`, cycles
read at each off MartyPC's counter.

IT IS DETERMINISTIC, which is what makes two builds comparable at all. The
package seeds OSAPI_RAND from the clock, so the harness breaks on
`tg_newgame` (reached by pressing N), writes a fixed seed into the kernel's
`osapi_seed` there, and from that stop on arms the worker's three sites. No
key is sent after that: the steering a player would do is POSTED into
`[tg_cmd]` at the `call tg_update` stop of scripted ticks - exactly the byte
W_ONKEY writes, spent by the same `tg_do_cmds` on the same tick. So the
pieces, the moves, the purges, the cascades and the game over arrive on the
same tick on any build that plays the same game.

`--board` (the default for the row) also lays a PREPARED board down at tick 0:
runs of eight and more butting straight onto runs of the other faction, a run
of sixteen, columns, a cross, and the ragged remains a settle has to close up.
Random play reaches a purge a few times in a thousand ticks and almost never
the shapes a run-length scan gets wrong; this reaches them on the first lock.

WHAT IT CHECKS, standalone:
  * at every checkpoint the matrix ON SCREEN is the grid in MEMORY - every
    AMBER cell solid, every GREEN one a dither, every empty one black, with
    the four cells of the falling piece the only extras allowed in PLAY (a
    piece erased in the wrong place, a flash page that misses a cell or a
    settle drawn from the wrong state all fail it). CGA and Hercules; VGA has
    no flat framebuffer to read.
  * the scenario reached a purge, a flash page and a settle step, so a pass
    that never went there is not reported as one;
  * the worker's stack slice: SPEC.md 8.3 fills every slice with 0xCC at
    spawn, so the first non-fill byte above the canary is how deep it has
    ever been, interrupts included (docs/FIELD-NOTES.md 42) - held to 75% of
    the class it declares.
  * --expect: the game the seed and the script play on the prepared board -
    THREAT, PURGES and the final grid's digest - which is a property of the
    RULES, so a scan that marks a run from the wrong cell (the first build of
    the size pass did, after any change of faction) fails it while every
    picture still agrees with its own grid.
With --ref, also: the SAME GAME (mode on every tick, the final grid, THREAT
and PURGES) and the same content box at every checkpoint, and the cycles per
tick kind against the reference, failing a kind more than --slack slower.
"""
import argparse
import hashlib
import json
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty, os88ui, os88geom                         # noqa: E402

CARDS = {"cga": "os8088_5150_cga_gla",
         "herc": "os8088_5150_herc_gla",
         "vga": "os8088_xt_vga"}
SEED = 0x2B67
HUD_H = 28
M_PLAY, M_PAUSE, M_OVER, M_FLASH, M_SETTLE = range(5)


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def pkg_syms(src):
    """Labels, bss offsets, equates and the worker's three call sites, by
    assembling `src` exactly as the Makefile does."""
    with tempfile.TemporaryDirectory() as d:
        mp, lp = os.path.join(d, "p.map"), os.path.join(d, "p.lst")
        subprocess.run(["nasm", "-f", "bin", "-w+error", "-I",
                        os.path.join(ROOT, "apps") + "/", "-l", lp,
                        "--before", "[map all %s]" % mp,
                        "-o", os.path.join(d, "p.bin"), src], check=True)
        syms, equ = {}, {}
        hexd = set("0123456789ABCDEF")
        for line in open(mp):
            f = line.split()
            if len(f) == 3 and set(f[1]) <= hexd:
                syms[f[2]] = int(f[1], 16)
            elif len(f) == 2 and len(f[0]) == 8 and set(f[0]) <= hexd:
                equ[f[1]] = int(f[0], 16)
        sites = {}
        for line in open(lp):
            f = line.split(None, 3)
            if len(f) < 4 or not set(f[1]) <= hexd:
                continue
            code = f[3].split(";")[0].split()
            if code[:1] != ["call"]:
                continue
            tgt = code[1] if len(code) > 1 else ""
            if tgt in ("tg_update", "tg_frame", "OSAPI_GFX_UNLOCK"):
                if tgt in sites:
                    raise SystemExit("tgplay: two `call %s` sites in %s"
                                     % (tgt, src))
                sites[tgt] = int(f[1], 16)
        return syms, equ, sites


class Grid:
    """The board in either layout the package has had: a byte a cell (the
    mark in bit 7), or a nibble a cell - even cells low - with the mark in
    bit 3 (TG_GRIDB = 512). Canonical form: faction | 0x80 if marked."""

    def __init__(self, equ):
        self.nib = equ.get("TG_GRIDB", 1024) == 512
        self.nbytes = 512 if self.nib else 1024

    def decode(self, raw):
        out = []
        for i in range(1024):
            if self.nib:
                v = (raw[i >> 1] >> (4 * (i & 1))) & 15
                out.append((v & 3) | (0x80 if v & 8 else 0))
            else:
                out.append(raw[i])
        return bytes(out)

    def encode(self, cells):
        if not self.nib:
            return bytes(cells)
        return bytes((cells[2 * k] & 3) | ((cells[2 * k + 1] & 3) << 4)
                     for k in range(512))


def board():
    """The prepared board: the shapes a run-length scan gets wrong."""
    g = [0] * 1024
    A, B = 1, 2

    def row(r, s):
        for c, ch in enumerate(s):
            g[r * 32 + c] = {"a": A, "b": B}.get(ch, 0)

    def col(c, r0, s):
        for i, ch in enumerate(s):
            g[(r0 + i) * 32 + c] = {"a": A, "b": B}.get(ch, 0)

    row(0, "aaabbbbbbbbaaaaaaaabbaaaaaaaaaaa")   # B8 after A3, A8, A11
    row(1, "bbbbbbbbbbbbbbbbaaaaaaaaaaaaaaaa")   # B16 then A16
    row(2, "ab.ab.ab.aaaaaaa.bbbbbbb.abababa")   # sevens: nothing
    row(30, "..........abbbbbbbbbbbbbbba.....")   # B15 between two As
    row(31, "aaaaaaaa.bbbbbbbbb..aaaaaaaaa.bb")   # A8, B9, A9
    col(0, 4, "bbbbbbbbbaaaaaaaa")                # B9 straight onto A8
    col(31, 5, "aaaaaaab.aaaaaaaaaaaaaaaa")       # A7 then A16
    col(29, 10, "bbbbbbbb")                       # crosses nothing
    for r in range(18, 26):                       # a cross: row 21 and
        g[r * 32 + 5] = A                         # column 5 share a cell
    row(21, "...aaaaaaaaa")
    for r in range(8, 12):                        # ragged leftovers for the
        row(r, "..ab....b...........a...b...a.")  # settle steps to close up
    return g


def script(tick):
    """The command posted at a tick, or 0. W_ONKEY's own codes (tg_onkey):
    1/2 A/D strafe, 3 rotate, 4 hard drop, 5-8 the four arrows."""
    if tick % 47 == 46:
        return 4
    pat = (5, 3, 6, 6, 7, 1, 8, 3, 2, 5, 5, 3)
    return pat[(tick // 3) % len(pat)] if tick % 3 == 0 else 0


def screen_check(m, card, seg, s, grid, mode):
    """The matrix on screen against the grid in memory. Returns (digest of
    the content box, list of complaints)."""
    ox, oy, cw, ch, csz, bx0 = (u16(m.readseg(seg, s[n], 2)) for n in (
        "tg_ox", "tg_oy", "tg_cwid", "tg_chgt", "tg_csz", "tg_bx0"))
    w, h, rows = m.vram(card)
    pix = bytes(b for r in rows[oy:oy + ch] for b in r[ox:ox + cw])
    digest = hashlib.sha1(pix).hexdigest()[:16]
    bad, extra = [], []
    if mode in (M_OVER, M_PAUSE):         # the banner is over the matrix
        return digest, bad
    ins = 1 if csz > 4 else 0
    # THE MATRIX'S BORDER RUNS THROUGH THE EDGE CELLS: tg_draw_board frames
    # bx0 .. bx0 + 32*csz - 1, which is the outer pixel line of row 0, row
    # 31, column 0 and column 31 - so on CGA, whose 3px cells have no gap, an
    # empty edge cell is never black. Those lines are not the cell's to judge.
    fx = (ox + bx0, ox + bx0 + 32 * csz - 1)
    fy = (oy + HUD_H, oy + HUD_H + 32 * csz - 1)
    for r in range(32):
        for c in range(32):
            v = grid[r * 32 + c]
            if v & 0x80:
                continue                  # a flash page: the phase decides
            x0 = ox + bx0 + c * csz + ins
            y0 = oy + HUD_H + r * csz + ins
            pts = [(x, y) for y in range(y0, y0 + csz - 2 * ins)
                   for x in range(x0, x0 + csz - 2 * ins)
                   if x not in fx and y not in fy]
            full = len(pts)
            lit = sum(rows[y][x] for x, y in pts)
            if v == 0:
                if lit:
                    extra.append((r, c))
            elif v == 1 and lit != full:
                bad.append("AMBER cell (%d,%d) has %d of %d lit"
                           % (r, c, lit, full))
            elif v == 2 and not 0 < lit < full:
                bad.append("GREEN cell (%d,%d) has %d of %d lit, not a dither"
                           % (r, c, lit, full))
    allowed = 4 if mode == M_PLAY else 0
    if len(extra) > allowed:
        bad.append("%d empty cells are lit (%d allowed): %s"
                   % (len(extra), allowed, extra[:8]))
    return digest, bad


def run(a):
    syms, equ, sites = pkg_syms(a.src)
    for need in ("tg_update", "tg_frame", "OSAPI_GFX_UNLOCK"):
        if need not in sites:
            raise SystemExit("tgplay: no `call %s` in %s" % (need, a.src))
    G = Grid(equ)
    card = a.card
    out = {"card": card, "ticks": a.ticks, "seed": SEED, "board": a.board,
           "rows": [], "shots": {}, "bad": []}
    with os88ui.boot(a.system, apps=a.games, machine=CARDS[card]) as ui:
        m = ui.m
        w = ui.path("B:/TAMEGRAM.O88")
        seg = u16(m.read(os88geom.winptr(m, w.i, ui.sym) + os88geom.W_SEG, 2))
        S = lambda n: seg * 16 + syms[n]       # noqa: E731
        # NOT ui.settle(): the game animates by design, so the screen never
        # stops. The worker being hired is the state that says it is playing.
        os88marty.until(m, lambda _: m.read(S("tg_hired"), 1)[0] == 1,
                        "TameGram's worker", poll=0.2, limit=120.0)
        ui.mo.to(4, 30)                       # out of the window's way
        upd = seg * 16 + sites["tg_update"]
        frm = seg * 16 + sites["tg_frame"]
        unl = seg * 16 + sites["OSAPI_GFX_UNLOCK"]
        seedp = m.sym("osapi_seed")
        st = {"tick": -1, "c0": 0, "c1": 0, "mode": 0, "done": False,
              "armed": False}

        def hit(mm, rec):
            a_ = rec["addr"]
            if not st["armed"]:
                mm.write(seedp, bytes([SEED & 255, SEED >> 8]))
                st["armed"] = True
                mm.breakpoints([{"type": "exec", "addr": x}
                                for x in (upd, frm, unl)])
                return None
            if a_ == upd:
                st["tick"] += 1
                if st["tick"] >= a.ticks:
                    st["done"] = True
                    mm.breakpoints([])
                    return None
                if st["tick"] == 0 and a.board:
                    # ...and owe a WHOLE frame, or the screen goes on
                    # showing the empty board until the first lock
                    mm.write(S("tg_grid"), G.encode(board()))
                    mm.write(S("tg_dirty"), b"\x01")
                    mm.write(S("tg_full"), b"\x01")
                st["c0"] = rec["cycles"]
                st["mode"] = mm.read(S("tg_mode"), 1)[0]
                c = script(st["tick"])
                if c:
                    mm.write(S("tg_cmd"), bytes([c]))
            elif a_ == frm and st["tick"] >= 0:
                st["c1"] = rec["cycles"]
                st["df"] = mm.read(S("tg_dirty"), 1)[0] | \
                    (mm.read(S("tg_full"), 1)[0] << 1)
            elif a_ == unl and st["tick"] >= 0 and "df" in st:
                t = st["tick"]
                out["rows"].append([t, st["mode"], st["df"],
                                    st["c1"] - st["c0"],
                                    rec["cycles"] - st["c1"]])
                del st["df"]
                if card != "vga" and t % a.every == a.every - 1:
                    mode = mm.read(S("tg_mode"), 1)[0]
                    grid = G.decode(mm.read(S("tg_grid"), G.nbytes))
                    d, bad = screen_check(mm, card, seg, syms, grid, mode)
                    out["shots"][str(t)] = d
                    out["bad"] += ["tick %d: %s" % (t, b) for b in bad]
            return None

        with os88marty.bp_trace(m, S("tg_newgame"), on_hit=hit) as tr:
            m.key("KeyN")
            tr.until(lambda: st["done"], "%d TameGram ticks" % a.ticks,
                     limit=1200.0)
        if a.png:                       # what the glass showed last
            w_, h_, data = m.fbuf(0)
            os88marty.write_png_rgb(a.png, w_, h_, data)
        grid = G.decode(m.read(S("tg_grid"), G.nbytes))
        out["grid"] = hashlib.sha1(grid).hexdigest()[:16]
        out["score"] = u16(m.read(S("tg_score"), 2))
        out["purges"] = u16(m.read(S("tg_purges"), 2))
        out["filled"] = sum(1 for b in grid if b)
        # THE WORKER'S SLICE: the deepest the 0xCC fill was ever overwritten
        base = m.sym("sch_stacks")
        import stkwater
        sizes = stkwater.slice_sizes(())
        mem = m.read(base, sum(sizes))
        out["slices"] = [(s_, used, sizes[s_ - 1])
                         for s_, used, free in stkwater.water(mem, len(sizes),
                                                              sizes)
                         if used is not None]
    return out


KINDS = (("play, piece moved", lambda r: r[1] == 0 and r[2] == 1),
         ("play, whole repaint", lambda r: r[1] == 0 and r[2] == 3),
         ("play, nothing owed", lambda r: r[1] == 0 and r[2] == 0),
         ("flash page", lambda r: r[1] == 3),
         ("settle step", lambda r: r[1] == 4),
         ("over / halt", lambda r: r[1] in (1, 2)))


def summary(o):
    res = {}
    for name, f in KINDS:
        rr = [r for r in o["rows"] if f(r)]
        if rr:
            res[name] = (len(rr), sum(r[3] for r in rr) / len(rr),
                         sum(r[4] for r in rr) / len(rr))
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--card", default="cga", choices=sorted(CARDS))
    ap.add_argument("--ticks", type=int, default=600)
    ap.add_argument("--every", type=int, default=25)
    ap.add_argument("--no-board", dest="board", action="store_false",
                    help="start from the empty board N leaves")
    ap.add_argument("--src", default=os.path.join(ROOT, "apps", "tamegram",
                                                  "tamegram.asm"))
    ap.add_argument("--system", default=os.path.join(ROOT, "build",
                                                     "os8088-360.img"))
    ap.add_argument("--games", default=os.path.join(ROOT, "build",
                                                    "games360.img"))
    ap.add_argument("--expect", metavar="THREAT:PURGES:GRID",
                    help="the game this seed and script must play: what the "
                    "row holds a build to with no reference run beside it")
    ap.add_argument("--png", help="the rendered screen at the end, for a "
                    "person to look at (VGA's only picture here)")
    ap.add_argument("--save")
    ap.add_argument("--ref")
    ap.add_argument("--slack", type=float, default=0.03,
                    help="fail a tick kind this much slower than --ref")
    a = ap.parse_args()
    o = run(a)
    if a.save:
        json.dump(o, open(a.save, "w"))
    s = summary(o)
    print("tgplay: %s, %d ticks%s, THREAT %d, PURGES %d, %d cells filled, "
          "grid %s" % (a.card, len(o["rows"]), ", prepared board"
                       if a.board else "", o["score"], o["purges"],
                       o["filled"], o["grid"]))
    bad = list(o["bad"])
    for sl, used, size in o["slices"]:
        print("tgplay: slot %d: %d of %d bytes ever used" % (sl, used, size))
        if size == 256 and used > 192:
            bad.append("the worker's slice reached %d of 256 - over 75%% of "
                       "the class it declares" % used)
    ref = json.load(open(a.ref)) if a.ref else None
    rs = summary(ref) if ref else {}
    for name, (n, cu, cf) in s.items():
        line = "tgplay: %-20s %4d ticks  update %8.0f  frame %9.0f cycles" \
            % (name, n, cu, cf)
        if name in rs:
            rn, ru, rf = rs[name]
            tot, rtot = cu + cf, ru + rf
            line += "   ref %8.0f %9.0f  (%+.2f%%)" % (
                ru, rf, 100.0 * (tot - rtot) / rtot if rtot else 0.0)
            if rtot and tot > rtot * (1 + a.slack) and n >= 3:
                bad.append("%s is %.1f%% slower than the reference"
                           % (name, 100.0 * (tot - rtot) / rtot))
        print(line)
    tot = sum(r[3] + r[4] for r in o["rows"])
    print("tgplay: all ticks %d cycles%s" % (
        tot, "" if not ref else "  ref %d (%+.2f%%)" % (
            sum(r[3] + r[4] for r in ref["rows"]),
            100.0 * (tot / max(1, sum(r[3] + r[4] for r in ref["rows"]))
                     - 1))))
    if a.board:
        for need in ("flash page", "settle step"):
            if need not in s:
                bad.append("the prepared board never reached a %s" % need)
        if not o["purges"]:
            bad.append("the prepared board never purged")
    if a.expect:
        want = a.expect.split(":")
        got = [str(o["score"]), str(o["purges"]), o["grid"]]
        if want != got:
            bad.append("the game is not the one this seed plays: THREAT, "
                       "PURGES and grid %s, expected %s"
                       % (":".join(got), a.expect))
    if a.card != "vga":
        print("tgplay: %d checkpoints: the matrix on screen against the grid"
              % len(o["shots"]))
    if ref:
        if (ref["card"], ref["ticks"], ref.get("board")) != \
                (o["card"], o["ticks"], o["board"]):
            bad.append("the reference is a different card, length or board")
        modes = [r[1] for r in o["rows"]]
        rmodes = [r[1] for r in ref["rows"]]
        if modes != rmodes:
            i = next((i for i, (x, y) in enumerate(zip(modes, rmodes))
                      if x != y), min(len(modes), len(rmodes)))
            bad.append("the game diverged at tick %d" % i)
        for k in ("grid", "score", "purges"):
            if o[k] != ref[k]:
                bad.append("%s differs: %r against %r" % (k, o[k], ref[k]))
        for t, d in sorted(o["shots"].items(), key=lambda x: int(x[0])):
            if ref["shots"].get(t) != d:
                bad.append("the content differs from the reference at tick %s"
                           % t)
                break
        print("tgplay: %d checkpoints compared with the reference"
              % len(o["shots"]))
    for b in bad:
        print("tgplay: FAIL: " + b)
    if not bad:
        print("tgplay: ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
