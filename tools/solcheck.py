#!/usr/bin/env python3
"""The Solitaire repaint gate (SPEC.md §43.9/§43.10) — same pixels, fewer fills.

    python3 tools/solcheck.py capture DIR IMG [machine]
    python3 tools/solcheck.py diff DIR-A DIR-B

Three changes have to be proved and not one of them can be proved by looking:

  §43.9.1 `WF_OWNBG` — the kernel stops white-filling the content in front of
  `W_PAINT`, so any pixel `sol_drawall` fails to write now shows whatever was
  there before. After a MOVE that is another window's pixels, which is why the
  session below drags the window rather than only repainting it in place.

  §43.9.2 the pile wipes inside a full `sol_drawall` are skipped, on the
  reasoning that the whole-content felt fill already covered every pile rect
  and pile rects are disjoint. If that reasoning is wrong the symptom is one
  pile's cards left standing inside another pile's slot — a real picture, not a
  blank one, so nothing about the screen will look broken.

  §43.10 the SHADOW — a drop redraws only the cards that moved, on the
  reasoning that a card is drawn out of (byte, x, y, visible height) and the
  ones below the change have all four unaltered. Its failure is the same
  species and worse: a card left standing where a card no longer is. That is
  what `PLAY` below is for, and it is the half the older session did not
  reach — it repainted and moved the window and never once dragged a CARD.
  Diff against `make SOLNOKEEP=1`, which keeps nothing at all.

**THE DEAL IS RANDOM, AND THAT IS THE WHOLE DIFFICULTY.** `sol_entry` seeds
from `OSAPI_GET_TICKS`, so two runs of the same binary deal differently and a
pixel diff between two builds says nothing. `sol_newgame` takes its entropy
from `OSAPI_RAND` and nowhere else, so this writes the kernel's own
`[osapi_seed]` to a fixed word and presses N: both builds then deal the SAME
game and every pixel is comparable.

**...and the MOVES have to be the same too.** They are chosen by reading the
package's own `sol_pcnt`/`sol_pile` out of the guest and picking the longest
legal run, so the sequence is a pure function of a deal both builds share —
rather than a script of coordinates, which would stop meaning anything the
first time the two builds' boards diverged. Every move is a real press, drag
and release through the UART, so `sol_grab`, `sol_drag` and `sol_drop` are all
in the path.

Captures are cropped to Solitaire's own frame, which is the scope of the claim
and also keeps the menu bar's clock out of the comparison.

    python3 tools/os88disk.py -o build/soltest.img --size 360 build/solitair.o88
"""
import os
import re
import struct
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import os88marty
from os88mouse import Mouse
from os88geom import W_SEG, WIN_SIZE
import sucheck as su
import subcheck as sc

SEED = 0x2A17                       # any fixed word; both builds get this one
TITLE_H = 18
PLAY = int(os.environ.get("SOLPLAY", "12"))     # moves to drag in the session

P_TABN, P_FND0, P_STOCK, P_WASTE = 7, 7, 11, 12
PILE_CAP, C_FACEUP, C_RANK = 24, 0x40, 0x0F

# The package's bss offsets, walked out of its own SWORD/SBYTE/SBUF run. There
# is no map for a package - the names are `equ`s over `os88_image_end`, which
# a NASM listing shows only inside the instructions that use them - so the walk
# is CHECKED against one such instruction below rather than trusted.
SOLCONST = {"P_TABN": P_TABN, "SOL_NPILE": 13, "PILE_CAP": PILE_CAP,
            "SOL_BACKMAX": 704, "SOL_SHW": 5 + PILE_CAP}


def solsyms():
    src = open(os.path.join(ROOT, "apps/solitaire/solitaire.asm")).read()
    blk = src[src.index("%assign SOL_BSS 0"):]
    off, syms = 0, {}
    for line in blk.splitlines():
        m = re.match(r"^(SWORD|SBYTE|SBUF)\s+([A-Za-z_]\w*)\s*(?:,\s*(.+))?$",
                     line.split(";")[0].strip())
        if not m:
            continue
        kind, name, arg = m.groups()
        syms[name] = off
        if kind == "SWORD":
            off += 2
        elif kind == "SBYTE":
            off += 1
        else:
            for k, v in SOLCONST.items():
                arg = arg.replace(k, str(v))
            off += eval(arg)
    img = os.path.getsize(os.path.join(ROOT, "build/solitair.bin"))
    syms = {k: img + v for k, v in syms.items()}
    lst = "/tmp/solcheck.lst"
    # THE LISTING HAS TO BE OF THE BUILD THAT IS ACTUALLY THERE, and the knob
    # is a make variable rather than an exported one - so the defines come from
    # the stamp the Makefile wrote beside the binary. Without this the walk is
    # checked against the OTHER build's listing, which the assertion below
    # catches only because the two images happen to differ in size.
    stamps = {"nokeep": "-DSOLNOKEEP", "keep": ""}
    defs = ""
    for n in os.listdir(os.path.join(ROOT, "build")):
        if n.startswith(".sol-"):
            defs = stamps.get(n[len(".sol-"):], "")
    os.system("nasm -f bin -w+error %s -I %s/apps/ -o /dev/null -l %s %s/apps/"
              "solitaire/solitaire.asm" % (defs, ROOT, lst, ROOT))
    txt = open(lst).read()
    i = txt.index("mov cl, [sol_pcnt+bx]")
    enc = re.search(r"\[([0-9A-F]{4})\]", txt[txt.rindex("\n", 0, i):i])
    want = int(enc.group(1)[2:4] + enc.group(1)[0:2], 16)
    if want != syms["sol_pcnt"]:
        raise SystemExit("solcheck: sol_pcnt is 0x%04x in the binary and "
                         "0x%04x in this walk - the bss layout moved"
                         % (want, syms["sol_pcnt"]))
    return syms


class Board:
    """The board, and enough of sol_colfan/sol_yoff/sol_pilexy to aim at it."""

    def __init__(self, m, seg, sym):
        self.m, self.seg, self.sym = m, seg, sym
        self.reload()

    def reload(self):
        m, seg, s = self.m, self.seg, self.sym
        self.cnt = list(m.readseg(seg, s["sol_pcnt"], 13))
        raw = m.readseg(seg, s["sol_pile"], 13 * PILE_CAP)
        self.pile = [list(raw[i * PILE_CAP:i * PILE_CAP + self.cnt[i]])
                     for i in range(13)]
        for n in ("sol_cw", "sol_ch", "sol_marg", "sol_topy", "sol_taby",
                  "sol_pitch", "sol_wstep", "sol_ox", "sol_oy", "sol_avail",
                  "sol_fand", "sol_fanu"):
            setattr(self, n[4:], struct.unpack(
                "<H", m.readseg(seg, s[n], 2))[0])
        self.wfan = m.readseg(seg, s["sol_wfan"], 1)[0]

    def colfan(self, p):
        cfa, cfu, n = self.fand, self.fanu, self.cnt[p]
        cfd = 0
        if n >= 2:
            while cfd < n and not (self.pile[p][cfd] & C_FACEUP):
                cfd += 1
            last, up = n - 1, max(0, n - 1 - cfd)
            need = up * cfu + cfd * cfa if up else last * cfa
            if need > self.avail:
                if up:
                    room = self.avail - cfd * cfa
                    if room > 0 and room >= up:
                        cfu = room // up
                    else:
                        cfu, rem = 1, self.avail - up
                        cfa = max(1, rem // cfd) if rem > 0 and cfd else 1
                else:
                    cfa = max(1, self.avail // last) if last else cfa
        return cfd, cfa, cfu

    def yoff(self, p, i):
        cfd, cfa, cfu = self.colfan(p)
        return i * cfa if i <= cfd else cfd * cfa + (i - cfd) * cfu

    def pilexy(self, p):
        col = 0 if p == P_STOCK else 1 if p == P_WASTE else \
            p if p < P_FND0 else p - (P_FND0 - 3)
        return (col * self.pitch + self.marg,
                self.taby if p < P_TABN else self.topy)


def rank(c):
    return c & C_RANK


def red(c):
    return ((c >> 4) & 3) in (1, 2)


def takes(b, dst, c):
    """May card `c` land on tableau column `dst`?"""
    if b.cnt[dst] == 0:
        return rank(c) == 12
    t = b.pile[dst][-1]
    return bool(t & C_FACEUP) and rank(t) == rank(c) + 1 and red(t) != red(c)


def find_move(b):
    """The LONGEST legal tableau run that has somewhere to go, else the waste's
    top card if it will go anywhere, else None.

    Longest on purpose: a multi-card hand is what makes the source column's
    keep interesting, and taking the same rule in both builds is what makes the
    two sessions the same session. A move that empties a column onto an empty
    column is refused, or the search oscillates for ever.
    """
    best, bestn = None, 0
    for src in range(P_TABN):
        for i in range(b.cnt[src]):
            c = b.pile[src][i]
            if not (c & C_FACEUP):
                continue
            run = b.pile[src][i:]
            if not all(rank(run[k]) == rank(run[k + 1]) + 1 and
                       red(run[k]) != red(run[k + 1])
                       for k in range(len(run) - 1)):
                continue
            for dst in range(P_TABN):
                if dst == src or not takes(b, dst, c):
                    continue
                if b.cnt[dst] == 0 and i == 0:
                    continue            # a whole column onto an empty one
                if len(run) > bestn:
                    best, bestn = (src, i, dst), len(run)
    if best:
        return best
    if b.cnt[P_WASTE]:
        c = b.pile[P_WASTE][-1]
        for dst in range(P_TABN):
            if takes(b, dst, c):
                return (P_WASTE, b.cnt[P_WASTE] - 1, dst)
    return None


def find_fnd(b):
    """A pile whose top card a foundation will take, played by the
    press-and-release-in-place gesture (§43.6) rather than by a drag. This is
    what empties a column outright, which is the case the shadow has to get
    right and a session of drags alone never reaches."""
    for p in list(range(P_TABN)) + [P_WASTE]:
        if not b.cnt[p]:
            continue
        c = b.pile[p][-1]
        if not (c & C_FACEUP):
            continue
        f = P_FND0 + ((c >> 4) & 3)
        if (rank(c) == 0 and not b.cnt[f]) or \
                (b.cnt[f] and rank(b.pile[f][-1]) + 1 == rank(c)):
            return p
    return None


def wshow(b):
    """sol_wshow: how many waste cards are fanned, 1..3, or 0 when empty."""
    n = b.cnt[P_WASTE]
    if not n:
        return 0
    return min(max(b.wfan, 1), 3, n)


def grabxy(b, src, idx):
    """Where to press to pick that card up, in SCREEN coordinates."""
    x, y = b.pilexy(src)
    if src == P_WASTE:
        x += (wshow(b) - 1) * b.wstep       # the top card sits fanned right
    else:
        y += b.yoff(src, idx)
    return b.ox + x + b.cw // 2, b.oy + y + 4


def shot(m, win, path, tag, mo=None):
    """The window's frame, IN GUEST COORDINATES, with the mouse cell blanked.

    Through `sucheck.fb` and not `m.fbuf`, which is the CARD's rasterised
    frame: on Hercules that is 720x350 for a 720x348 screen at dx = -16, so a
    crop taken at guest coordinates samples sixteen columns of whatever is
    beside the window. Both builds are offset the same way, so a cross-build
    diff survives it — the self-check below does not, because the window has
    MOVED between its two captures and the two mis-crops are then different.

    The arrow is PARKED off the window first, and blanking it where it lands
    is then belt and braces. Blanking alone is not enough here and that cost a
    round: the pointer is somewhere different at each capture, so one image
    gets a black box where the other has cards and the difference is the mask
    rather than the drawing — 321 pixels of it, in BOTH builds, which is what
    said it was the harness. `subcheck.shot` can blank in place because its two
    runs are the same session at the same step; the self-check below compares
    two DIFFERENT steps.
    """
    if mo is not None:
        px = win.x - 40 if win.x >= 40 else win.x + win.w + 24
        mo.to(max(0, px), max(su.DESK_ZY0 // 2, win.y + 60))
        time.sleep(0.5)
    w, bpp, data = su.fb(m)
    h = len(data) // (w * bpp)
    x1, y1 = max(0, win.x), max(0, win.y)
    x2, y2 = min(w, win.x + win.w), min(h, win.y + win.h)
    cw = x2 - x1
    body = bytearray()
    for y in range(y1, y2):
        body += data[(y * w + x1) * bpp:(y * w + x2) * bpp]
    if mo is not None:
        cx1, cy1, cx2, cy2 = sc.curbox(mo)
        for y in range(max(cy1, y1), min(cy2, y2 - 1) + 1):
            r = (y - y1) * cw * bpp
            for x in range(max(cx1, x1), min(cx2, x2 - 1) + 1):
                for k in range(bpp):
                    body[r + (x - x1) * bpp + k] = 0
    with open(path, "wb") as f:
        f.write(struct.pack("<5H", x1, y1, cw, y2 - y1, bpp))
        f.write(bytes(body))
    print("  %-10s %dx%d at (%d,%d), %d bytes"
          % (tag, cw, y2 - y1, x1, y1, len(body)))


def capture(outdir, img, machine):
    os.makedirs(outdir, exist_ok=True)
    with os88marty.launch(os.path.join(ROOT, "build/os8088-360.img"),
                          apps=img, machine=machine) as m:
        mo = Mouse(marty=m)
        print("machine %s / %s -> %s" % (machine, img, outdir))
        mo.dblclick(*su.zone(m, 1))
        time.sleep(4)
        disk = [w for w in su.windows(m) if w.visible][0]
        mo.dblclick(*su.row(disk, 0))
        time.sleep(30)
        sol = [w for w in su.windows(m) if w.visible
               and w.title.upper().startswith("SOL")]
        if not sol:
            raise SystemExit("solcheck: Solitaire did not launch off %s" % img)
        sol = sol[0]
        print("solitaire %r" % (sol,))

        # the same deal in both builds
        m.write(m.sym("osapi_seed"), struct.pack("<H", SEED))
        sc.pclick(mo, sol.x + sol.w // 2, sol.y + sol.h - 30)
        os88marty.settle(m)
        m.write(m.sym("osapi_seed"), struct.pack("<H", SEED))
        m.key("KeyN")
        time.sleep(3)
        os88marty.settle(m)
        s = [w for w in su.windows(m) if w.visible and w.i == sol.i][0]
        shot(m, s, os.path.join(outdir, "deal.raw"), "deal", mo)

        # ...and a MOVE, which is a kernel-driven full repaint: the raise
        # cache is dropped for the front window, so W_PAINT runs - with no
        # white fill in front of it once WF_OWNBG is promised
        p = sc.titlebar(m, s)
        sc.pdrag(mo, p[0], p[1], p[0] - 40, p[1] + 24)
        os88marty.settle(m)
        s = [w for w in su.windows(m) if w.visible and w.i == sol.i][0]
        print("moved to %r" % (s,))
        shot(m, s, os.path.join(outdir, "moved.raw"), "moved", mo)

        # ...and once more in place, so a pile left inside another pile's slot
        # is compared against the same position drawn a second time
        m.key("KeyR")                       # Restart Deal: the SAME deal
        time.sleep(3)
        os88marty.settle(m)
        s = [w for w in su.windows(m) if w.visible and w.i == sol.i][0]
        shot(m, s, os.path.join(outdir, "restart.raw"), "restart", mo)

        # --- §43.10: PLAY, and capture after every move ----------------------
        #
        # Each capture is the whole content, so a card left standing anywhere
        # in it shows up - and capturing after EVERY move names which one
        # broke rather than only that something did. The moves come out of the
        # guest's own board, so a deal both builds share gives both the same
        # sequence without a script of coordinates.
        seg = struct.unpack("<H", m.read(m.sym("wm_wins") + s.i * WIN_SIZE +
                                         W_SEG, 2))[0]
        b = Board(m, seg, solsyms())
        dry = 0
        for step in range(PLAY):
            mv = find_move(b)
            if mv is None:
                fp = find_fnd(b)
                if fp is not None:
                    px, py = grabxy(b, fp, b.cnt[fp] - 1)
                    print("  step %2d: %s top card -> its foundation" %
                          (step, "waste" if fp == P_WASTE else "col %d" % fp))
                    sc.pclick(mo, px, py)
                    os88marty.settle(m)
                    s = [w for w in su.windows(m)
                         if w.visible and w.i == sol.i][0]
                    shot(m, s, os.path.join(outdir, "play%02d.raw" % step),
                         "play%02d" % step)
                    b.reload()
                    dry = 0
                    continue
                # nothing to play: deal. The stock is 24 cards, so a dead game
                # needs a full cycle of it before the session gives up
                m.key("Space")
                time.sleep(2)
                os88marty.settle(m)
                b.reload()
                dry += 1
                if dry > 24:
                    print("  no move left after %d step(s)" % step)
                    break
                continue
            dry = 0
            src, idx, dst = mv
            n = b.cnt[src] - idx
            px, py = grabxy(b, src, idx)
            qx, qy = b.pilexy(dst)
            qx = b.ox + qx + b.cw // 2
            qy = b.oy + qy + (b.yoff(dst, b.cnt[dst]) if b.cnt[dst] else 0) + 4
            print("  step %2d: %s card %d (%d) -> col %d" %
                  (step, "waste" if src == P_WASTE else "col %d" % src,
                   idx, n, dst))
            sc.pdrag(mo, px, py, qx, qy)
            os88marty.settle(m)
            s = [w for w in su.windows(m) if w.visible and w.i == sol.i][0]
            shot(m, s, os.path.join(outdir, "play%02d.raw" % step),
                 "play%02d" % step)
            b.reload()

        # Auto Finish: every card that will go to a foundation, a move a tick
        # (§43.6) - a long run of sol_domove with no drag in it at all, which
        # is the other way a column empties
        m.key("KeyA")
        time.sleep(6)
        os88marty.settle(m)
        s = [w for w in su.windows(m) if w.visible and w.i == sol.i][0]
        shot(m, s, os.path.join(outdir, "play98_auto.raw"), "auto", mo)

        # ...and one forced FULL repaint at the end. The incremental content
        # and the same position drawn from scratch must be the same picture,
        # which is the claim in its purest form and needs no second build.
        p = sc.titlebar(m, s)
        sc.pdrag(mo, p[0], p[1], p[0] + 12, p[1] - 6)
        os88marty.settle(m)
        s = [w for w in su.windows(m) if w.visible and w.i == sol.i][0]
        shot(m, s, os.path.join(outdir, "played_repaint.raw"), "repainted", mo)
        m.quit()


def pixels(path):
    d = open(path, "rb").read()
    return struct.unpack("<5H", d[:10]), d[10:]


def differ(ba, bb, bpp):
    """Differing PIXELS, not bytes - `vram` is a byte and `fbuf` is rgb24."""
    return sum(1 for k in range(0, min(len(ba), len(bb)), bpp)
               if ba[k:k + bpp] != bb[k:k + bpp])


def selfcheck(d):
    """Within ONE build: the played position, incremental, against the same
    position forced through a full W_PAINT. The origin moved, so only the
    pixels are compared - which is exactly the claim, the window having been
    dragged precisely to make the kernel order that repaint."""
    plays = sorted(f for f in os.listdir(d) if f.startswith("play")
                   and f != "played_repaint.raw")
    ref = os.path.join(d, "played_repaint.raw")
    if not plays or not os.path.exists(ref):
        return 0
    ha, ba = pixels(os.path.join(d, plays[-1]))
    hb, bb = pixels(ref)
    if ha[2:] != hb[2:] or len(ba) != len(bb):
        print("%-18s SIZE DIFFERS %s vs %s" % ("[self] repaint", ha, hb))
        return 1
    n = differ(ba, bb, ha[4])
    print("%-18s %dx%d  %d differing pixels  (%s vs a full repaint)"
          % ("[self] repaint", ha[2], ha[3], n, plays[-1][:-4]))
    return 1 if n else 0


def diff(a, b):
    bad = 0
    names = sorted(set(f for f in os.listdir(a) if f.endswith(".raw")) |
                   set(f for f in os.listdir(b) if f.endswith(".raw")))
    order = {"deal.raw": 0, "moved.raw": 1, "restart.raw": 2}
    names.sort(key=lambda n: (order.get(n, 3), n))
    for name in names:
        pa, pb = os.path.join(a, name), os.path.join(b, name)
        if not (os.path.exists(pa) and os.path.exists(pb)):
            print("%-18s MISSING from %s" %
                  (name, b if os.path.exists(pa) else a))
            bad += 1
            continue
        ha, ba = pixels(pa)
        hb, bb = pixels(pb)
        if ha != hb:
            print("%-18s GEOMETRY DIFFERS %s vs %s" % (name, ha, hb))
            bad += 1
            continue
        n = differ(ba, bb, ha[4])
        print("%-18s %dx%d  %d differing pixels" % (name, ha[2], ha[3], n))
        bad += 1 if n else 0
    bad += selfcheck(a)
    bad += selfcheck(b)
    print("PASS" if not bad else "FAIL")
    return 0 if not bad else 1


def main():
    if len(sys.argv) >= 4 and sys.argv[1] == "capture":
        machine = sys.argv[4] if len(sys.argv) > 4 else "os8088_5150_cga_gla"
        capture(sys.argv[2], sys.argv[3], machine)
        return 0
    if len(sys.argv) == 4 and sys.argv[1] == "diff":
        return diff(sys.argv[2], sys.argv[3])
    raise SystemExit(__doc__)


if __name__ == "__main__":
    sys.exit(main())
