#!/usr/bin/env python3
"""Gorillas' field on the packed desktop: a blue sky and solid buildings
(SPEC.md 120.5, the package rule for DH).
    make vesatest && python3 tests/vesagor.py [--png out.png]

Boots build/vesafsx.img - tests/vesafsx.py's disk, whose \\AUTOSTRT link
opens Gorillas on the 1024x768x16 desktop - and plays the setup through:
seven Enters keep every question's default and an eighth takes "P = Play
game", which draws the skyline. Then it reads the field off a screendump.

WHAT IT ASSERTS: the window's blue (CBLUE) region is the size of a field
and not of a three-line HUD; most of its rows are sky, edge to edge; and
in its lower third a building colour runs solid for dozens of pixels.

THE RED IT WAS WRITTEN AGAINST: OSAPI_WM_DISPLAY answers DH = 16 on this
display, and Gorillas' tables are keyed 1 / 2 / 4 - with no "4" to match,
its band composer packed the scene as 2-bit CGA rows and handed them to
OSAPI_GFX_BLIT4 as 4-bit ones: a white sky under the HUD and every building
a comb of one-pixel columns (10 sky rows, no run past 14 px, where the VGA
desktop draws 97 and 68). Gorillas folds kind 4 to VGA at depth 4 now.
QEMU by name, vesa's reason.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(ROOT, "tests"))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import heapmap                                              # noqa: E402
import os88geom                                             # noqa: E402
import os88qemu                                             # noqa: E402
import shot                                                 # noqa: E402
import vesa                                                 # noqa: E402
import vesafsx                                              # noqa: E402
import os88sym                                              # noqa: E402
from pxsyms import instance                                 # noqa: E402
import subprocess                                           # noqa: E402
import tempfile                                             # noqa: E402

BLUE = (0, 0, 170)                  # CBLUE as QEMU lands the 565 word


def gor_syms():
    """{name: offset} and the image bytes of apps/gorillas/gorillas.asm as it
    is (tests/pxsyms.py's shape, for this package)."""
    with tempfile.TemporaryDirectory() as d:
        cp, mp = os.path.join(d, "g.asm"), os.path.join(d, "g.map")
        open(cp, "w").write(open(os.path.join(ROOT, "apps/gorillas/gorillas.asm")).read()
                            + "\n[map symbols %s]\n" % mp)
        subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", "apps/", "-I", "apps/gorillas/",
                        "-o", os.path.join(d, "g.bin"), cp], cwd=ROOT, check=True)
        out = {}
        for L in open(mp):
            f = L.split()
            if len(f) == 3 and all(c in "0123456789ABCDEF" for c in f[0]):
                out[f[2]] = int(f[1], 16)
        return out, open(os.path.join(d, "g.bin"), "rb").read()
WHITE = (255, 255, 255)


def q(c):
    """A QEMU 565 -> 888 channel, back to the EGA's four levels."""
    return min((0, 85, 170, 255), key=lambda v: abs(v - c))


def ega(pix, w, x, y):
    return tuple(q(c) for c in vesa.px(pix, w, x, y))


def field(pix, sw, sh):
    """(box w, box h, sky rows, longest solid building run) of the window's
    blue region below the menu bar - the HUD band and, when it is drawn, the
    sky above the skyline."""
    xs, ys = [], []
    for y in range(24, sh, 2):
        for x in range(0, sw, 2):
            if ega(pix, sw, x, y) == BLUE:
                xs.append(x)
                ys.append(y)
    if not xs:
        return 0, 0, 0, 0
    x1, x2, y1, y2 = min(xs), max(xs), min(ys), max(ys)
    bw, bh = x2 - x1 + 1, y2 - y1 + 1
    sky = 0
    for y in range(y1, y2 + 1):
        n = sum(1 for x in range(x1, x2 + 1) if ega(pix, sw, x, y) == BLUE)
        if n >= 0.9 * bw:
            sky += 1
    best = 0
    for y in range(y1 + 2 * bh // 3, y2 + 1):
        run, last = 0, None
        for x in range(x1, x2 + 1):
            c = ega(pix, sw, x, y)
            if c == last and c not in (BLUE, WHITE):
                run += 1
                best = max(best, run)
            else:
                run, last = 1, c
    return bw, bh, sky, best


def keypath(q, kb0, B, W, seg, image):
    """WHERE AN ENTER WENT, off the guest: the BIOS buffer either side (a
    press the ROM never stored leaves the tail where it was, one the UI task
    never fetched leaves the head behind it), the kernel's key-state map's
    Alt and Enter bits and its Alt+Enter latch (SPEC.md 9.7, 9.7.1), the
    ROM's shift flags, the fsx bracket, the screen saver, whether Gorillas'
    window is the frontmost, and whether the instance is still where this row
    reads it."""
    S = os88sym.linear
    kb = q.read(0x41A, 4)
    dn = q.read(S("kbd_dnmap"), 16)
    top = os88geom.top(q, S)
    now = instance(q, S, image)
    return ("buffer head/tail %d/%d -> %d/%d, map Alt %d Enter %d, latch "
            "%02X, shift flags %02X, gr_fs %d, fsx_task %02X, saver %d, "
            "front window %s, instance %s, state %d field %d"
            % (kb0[0] | kb0[1] << 8, kb0[2] | kb0[3] << 8,
               kb[0] | kb[1] << 8, kb[2] | kb[3] << 8,
               dn[0x38 >> 3] >> (0x38 & 7) & 1, dn[0x1C >> 3] >> (0x1C & 7) & 1,
               q.read(S("kbd_ae"), 1)[0], q.read(0x417, 1)[0], B("gr_fs"),
               q.read(S("fsx_task"), 1)[0], q.read(S("blk_on"), 1)[0],
               "Gorillas'" if top == W("gr_win") + (os88sym.KERNEL_SEG << 4)
               else "%05X" % top,
               "unmoved" if now == seg else "now at %s" % now,
               B("gr_state"), B("gr_setupfield")))


def shot_fail(q, out_png):
    """The glass as the row gave up, kept where a person can look at it."""
    sw, sh, pix = vesa.screendump(q, os.path.join(ROOT, "build", "vesa-shot"))
    path = out_png or os.path.join(ROOT, "build", "vesagor-fail-%d.png" % os.getpid())
    shot.png(path, sw, sh, pix)
    print("vesagor: the screen as it failed: %s" % os.path.relpath(path, ROOT))


def main():
    out_png = None
    img = "build/vesafsx.img"
    args = sys.argv[1:]
    while args:
        a = args.pop(0)
        if a == "--png":
            out_png = args.pop(0)
        elif a == "--img":
            img = args.pop(0)
    vesa.kill_stale()
    vesafsx.launch(img)
    fails = []
    try:
        q_ = heapmap.Qmp(vesa.SOCK)
        vesa.desk_up(q_, fails, autostart=False)    # (guest clock: vesa.py)
        # --- Gorillas' instance, and its setup read off its own state ------
        # (not off the glass: the caret blinks, so a panel is never still,
        # and the splash and the first question are both a blue page - the
        # fixed-pace first cut typed an Enter into the splash and the eighth
        # landed on the choice page)
        syms, image = gor_syms()
        os88qemu.acted(q_, lambda: instance(q_, os88sym.linear, image), 120.0,
                       "Gorillas' instance")
        seg = instance(q_, os88sym.linear, image)
        if not seg:
            fails.append("no Gorillas instance: the autostart link did not open it")
            raise SystemExit(0)
        B = lambda n: q_.read(seg * 16 + syms[n], 1)[0]            # noqa: E731
        W = lambda n: int.from_bytes(q_.read(seg * 16 + syms[n], 2),  # noqa: E731
                                     "little")
        if not os88qemu.acted(q_, lambda: B("gr_state") == 5, 60.0,
                              "the setup page"):
            fails.append("Gorillas never reached its setup page (state %d)"
                         % B("gr_state"))
            raise SystemExit(0)
        # ...and the UI FINISHED with the launch before the first Enter: the
        # worker writes state 5 and THEN paints the page, under the lock the
        # key's dispatch takes, so this is a wait the key would make anyway
        os88qemu.ui_done(q_, os88sym.linear, cap=5.0, what="the setup page")
        # EACH ENTER IS ONE QUESTION, CONFIRMED BEFORE THE NEXT GOES - and a
        # miss stops the row there and names where the key went. The loop
        # waited `gr_setupfield > i` and carried on either way, so ONE lost
        # Enter failed all seven waits and read as "not one of the seven
        # reached Gorillas" (docs/plans/SOAK-PARALLEL.md 17.11: one Enter
        # dropped on purpose reproduces that line exactly)
        for i in range(7):                  # the seven questions, at their defaults
            want = i + 1
            kb0 = q_.read(0x41A, 4)
            q_.hmp("sendkey ret")
            if not os88qemu.acted(
                    q_, lambda: B("gr_setupfield") >= want or B("gr_state") != 5,
                    15.0, "question %d answered" % want):
                fails.append("Enter %d of 7 did not answer question %d: %s"
                             % (want, want, keypath(q_, kb0, B, W, seg, image)))
                shot_fail(q_, out_png)
                raise SystemExit(0)
            if B("gr_state") == 5 and B("gr_setupfield") != want:
                fails.append("Enter %d of 7 answered %d questions: a key was "
                             "taken twice" % (want, B("gr_setupfield")))
                raise SystemExit(0)
        if B("gr_state") != 6:
            fails.append("after seven Enters the state is %d, not the choice page (6)"
                         % B("gr_state"))
            raise SystemExit(0)
        kb0 = q_.read(0x41A, 4)
        q_.hmp("sendkey ret")               # ...and Play, which draws the skyline
        if not os88qemu.acted(q_, lambda: B("gr_state") < 4, 15.0, "the game"):
            fails.append("the eighth Enter did not start the game: %s"
                         % keypath(q_, kb0, B, W, seg, image))
            shot_fail(q_, out_png)
            raise SystemExit(0)
        # ...band by band, under a loaded host for longer than one pace: read
        # the field until the buildings are there, or 30 s have gone, and
        # judge what is on the glass then (the comb never satisfies this)
        # (30 GUEST seconds: it was the host's, which a loaded box gave
        # the guest less of)
        c = os88qemu.Clock(q_)
        while True:
            os88qemu.pace(q_, 3.0)
            sw, sh, pix = vesa.screendump(q_, os.path.join(ROOT, "build", "vesa-shot"))
            bw, bh, sky, run = field(pix, sw, sh)
            if run >= 32 or c.secs() > 30 or c.stalled():
                break
        if out_png:
            shot.png(out_png, sw, sh, pix)
        if (sw, sh) != (vesa.WANT_W, vesa.WANT_H):
            fails.append("screen is %dx%d, want %dx%d" % (sw, sh, vesa.WANT_W, vesa.WANT_H))
        print("vesagor: blue region %dx%d, %d sky rows, longest solid building run %d px"
              % (bw, bh, sky, run))
        if bw < 400 or bh < 150:
            fails.append("the window's blue region is %dx%d: a HUD band and no sky "
                         "(the field wants hundreds of rows of CBLUE)" % (bw, bh))
        if sky < 40:
            fails.append("%d rows of sky edge to edge, want 40 or more: the sky is "
                         "not CBLUE" % sky)
        if run < 32:
            fails.append("the longest solid run of a building colour in the lower "
                         "third is %d px, want 32 or more: the buildings are a comb "
                         "(2-bit rows blitted as 4-bit)" % run)
        if fails and not out_png:           # ...and the picture they are about
            path = os.path.join(ROOT, "build", "vesagor-fail-%d.png" % os.getpid())
            shot.png(path, sw, sh, pix)
            print("vesagor: the screen as it failed: %s" % os.path.relpath(path, ROOT))
    except SystemExit:
        pass
    finally:
        vesa.kill_stale()
    if fails:
        print("vesagor: FAIL")
        for f in fails:
            print("  " + f)
        return 1
    print("vesagor: ok - Gorillas' sky and skyline on the 1024x768x16 desktop")
    return 0


if __name__ == "__main__":
    sys.exit(main())
