"""DrMarco's screenshots, for tools/guide/manuals/DRMARCO_O88.md.

    python3 tools/guide/capture/run.py DRMARCO_O88 --apps build/games360.img

The game itself runs in a 320x240 "Mode X" screen, and MartyPC's display
capture loses the bottom 40 rows of that mode. So the play shot is decoded
straight out of the four VGA planes with tests/drmario.py's own reader and
written at twice the size, the way the screen looks on a 640x480 monitor.
"""
import os
import sys

import os88marty

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..", "..")
sys.path.insert(0, os.path.join(ROOT, "tests"))
import drmario as DM  # noqa: E402

OUT = os.path.join(ROOT, "build", "guide", "shots")


def _tap(m, name, secs=0.12):
    m.key(name, up=False)
    os88marty.guest_sleep(m, 0.05)
    m.key(name, down=False)
    os88marty.guest_sleep(m, secs)


def _hold(m, name, secs):
    m.key(name, up=False)
    os88marty.guest_sleep(m, secs)
    m.key(name, down=False)
    os88marty.guest_sleep(m, 0.08)


def _native(ui, p, sid):
    m = ui.m
    m.pause()
    m.bp_exec(p.addr("input"))
    m.run()
    m.wait_stop(30)
    raw = DM.video(p, "vga")
    im = DM.native_image(p, "vga", raw)
    im = im.resize((im.width * 2, im.height * 2))
    im.save(os.path.join(OUT, sid + ".png"))
    print("shot %s %dx%d" % (sid, im.width, im.height))
    m.bp_exec()
    m.run()


def _choose(board, colors):
    """The column (0-6) and order for a flat capsule that lines up most
    cells of its colours below and beside it."""
    def free(x, y):
        return board[y * 8 + x] == 0

    def col(x, y):
        return board[y * 8 + x] & 3 if board[y * 8 + x] else 0

    best = None
    for swap in (0, 1):
        a, b = (colors[0] & 3, colors[1] & 3)
        if swap:
            a, b = b, a
        for x in range(7):
            y = -1
            while y + 1 < 16 and free(x, y + 1) and free(x + 1, y + 1):
                y += 1
            if y < 0:
                continue
            score = 0
            for cx, c in ((x, a), (x + 1, b)):
                yy = y + 1
                while yy < 16 and col(cx, yy) == c:
                    score += 2
                    yy += 1
            if x > 0 and col(x - 1, y) == a:
                score += 1
            if x < 6 and col(x + 2, y) == b:
                score += 1
            if a == b:
                score += 1
            if y < 5:
                score -= 10
            key = (score, y, -abs(x - 3))
            if best is None or key > best[0]:
                best = (key, x, swap)
    return (best[1], best[2]) if best else (3, 0)


def capture(ui, shot):
    sym = DM.symbols()
    ui.path("B:/DRMARCO.O88")
    for o in ui.windows():
        if o.visible and o.title != "DrMarco":
            ui.close(o)
    ui.mo.to(620, 460)
    os88marty.guest_sleep(ui.m, 8.0)
    shot("drmarco-splash", window="DrMarco", pad=8)

    # The help page, and back.
    _tap(ui.m, "KeyH")
    os88marty.guest_sleep(ui.m, 8.0)
    shot("drmarco-help", window="DrMarco", pad=8)
    _tap(ui.m, "KeyH")
    os88marty.guest_sleep(ui.m, 1.0)

    # Level 3 at LOW speed.
    for _ in range(3):
        _tap(ui.m, "ArrowRight")
    os88marty.guest_sleep(ui.m, 0.5)

    p = DM.Probe(ui, sym)
    _tap(ui.m, "Enter")
    os88marty.guest_sleep(ui.m, 2.0)
    # Play a few capsules with a small greedy player that reads the bottle
    # out of the game's memory, and stop while the bottle still has room.
    last = None
    for n in range(10):
        for _ in range(300):
            st = p.b("state")
            if st >= 3 or (st == 0 and p.b("pillcount") != last):
                break
            os88marty.guest_sleep(ui.m, 0.05)
        if p.b("state") != 0:
            break
        last = p.b("pillcount")
        board = bytes(p.data("board", 128))
        if n >= 7 or any(0 < c < 0x50 for c in board[:8 * 6]):
            break                       # stop while the game is still going
        x, swap = _choose(board, p.data("colors", 2))
        if swap:
            _tap(ui.m, "KeyX")
            _tap(ui.m, "KeyX")
        step = "ArrowLeft" if x < 3 else "ArrowRight"
        for _ in range(abs(x - 3)):
            _tap(ui.m, step, 0.08)
    os88marty.guest_sleep(ui.m, 0.4)
    _native(ui, p, "drmarco-play")

