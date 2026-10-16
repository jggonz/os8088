#!/usr/bin/env python3
"""SPEC.md 110.14: the Desk Clock - the clock on the desk, its nine faces,
its settings window and its alarm, the state file between them.

    make && python3 tests/deskclock.py [--leg vga|herc]

The SHIPPED 360KB system disk copied to a scratch file, on MartyPC. Offsets
into the package's bss and payload are tools/os88stik.py's DESKCLOK_*, which
t_stik holds to deskclok.asm's own numbers; the state file is flushed off the
emulated drive and parsed by tools/os88stik.py.

vga (default) - an XT with a VGA: the TALL layout, four planes
  A  launched from A:/STIKAPPS, the clock is a DESK window, Medium Classic
     (a 128-px card) at its default place, and the dial's corners are the
     DESKTOP'S dither, phase and all: a corner pixel matches the desktop two
     pixels left of the frame and differs from the one beside it (the
     ground is sampled off the glass and composed, SPEC.md 110.14.4).
  B  S shows the seconds: the dial changes from one second to the next (the
     second hand's box is redrawn) while the clock is otherwise still.
  C  Face > Red LED: the face is lit red, and the file says face 6.
  D  Enter opens the settings window beside the clock; the Neon '90 tile
     PREVIEWS on the desk (the dial navy) and Esc - Cancel - puts the Red
     LED back; the Classic tile and OK keep Classic, and the file says so.
  E  F - `Clock face only` - shrinks the frame to the face and its tag row,
     and the tag row at rest is the desktop's ground (no plate).
  F  the guest's clock set to 06:59:50, the alarm window (A) set to 07:00
     and on: the file says so; at 07:00 it RINGS - the frame grows for its
     rows, face-only or not - and Z snoozes it: quiet, snoozed, the frame
     back to the face.
  G  a reset, and the clock comes back from its Sticky App row: Classic,
     face only, the alarm set.

herc - a 5150 with a Hercules card: the MID layout, one plane
  H  the Red LED face on 1bpp: its segments lit white on the black face,
     its unlit segments dotted, and the ground beside the face the desktop's.

**BREAK IT ON PURPOSE**: take dk_gsample's call out of dk_pcard and A's
corner reads white; drop dk_srestore from dk_scancel and D's Cancel keeps
Neon; take dk_alarm's call out of dk_tick and F never rings.
"""
import os
import shutil
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import os88ui      # noqa: E402
import os88marty   # noqa: E402
import os88geom as geom   # noqa: E402
import os88flush   # noqa: E402
import os88stik    # noqa: E402

ROOT = os.path.join(HERE, "..")
BDIR = os.path.join(ROOT, os.environ.get("OS88_BUILD", "build"))
TITLE = "Desk Clock"
S = os88stik
STIK_ST = S.stik_st_off(S.DESKCLOK_STIK_BSS, S.DESKCLOK_PAYLEN)
STIK_S_DIRTY = 16
PAY = S.stik_hdr_off(S.DESKCLOK_STIK_BSS) + S.STIK_HDR_SIZE
RED, NAVY = (255, 85, 85), (0, 0, 170)

fails = []


def check(cond, what):
    print("  %s: %s" % ("ok" if cond else "FAIL", what))
    if not cond:
        fails.append(what)
    return cond


def image_size():
    b = open(os.path.join(BDIR, "deskclok.bin"), "rb").read()
    return struct.unpack_from("<H", b, 8)[0]


class Clock(object):
    """The running Desk Clock: its window, its bss, its state file."""

    def __init__(self, ui, tmp):
        self.ui, self.m, self.tmp = ui, ui.m, tmp
        self.image = image_size()
        self.seg = None

    def find(self):
        for o in self.ui.windows():
            if o.title == TITLE and o.flags & geom.WF_DESK:
                return o
        return None

    def bind(self):
        w = self.find()
        if w is None:
            raise SystemExit("no desk window called %r: %r"
                             % (TITLE, self.ui.windows()))
        self.seg = struct.unpack_from("<H", self.m.read(
            self.ui._S("wm_wins") + w.i * geom.WIN_SIZE + geom.W_SEG, 2))[0]
        return w

    def launch(self):
        ui = self.ui
        w = ui.path("A:/STIKAPPS/DESKCLOK.O88")
        check(w is not None and w.title == TITLE, "Desk Clock opened")
        for o in list(ui.windows()):        # the folder windows go: the clock
            if o.title != TITLE and o.visible:     # is UNDER every window
                ui.close(o)
        w = self.bind()
        os88marty.guest_sleep(self.m, 2.0)      # the first beat
        return w

    def bss(self, off, n=1):
        return self.m.readseg(self.seg, self.image + off, n)

    def byte(self, off):
        return self.bss(off)[0]

    def word(self, off):
        return struct.unpack("<H", bytes(self.bss(off, 2)))[0]

    def pay(self):
        return bytes(self.bss(PAY, S.DESKCLOK_PAYLEN))

    def face(self):
        w = self.find()
        return w.x + 1, w.y

    def key(self, name, wait=3.0):
        self.m.key(name)
        os88marty.guest_sleep(self.m, wait)

    def activate(self):
        w = self.find()
        self.ui.mo.click(w.x + w.w // 2, w.y + 20, settle=0)
        os88marty.pace(self.m, 1.0)

    def raw(self, path):
        out = os.path.join(self.tmp, "flushed.img")
        self.m.pause()
        try:
            self.m.flush(0, out)
        finally:
            self.m.run()
        v = os88flush.Volume(open(out, "rb").read())
        try:
            return v.read(path)
        except Exception:
            return None

    def saved(self, what):
        """Wait for the save a change owes, then the state file off A:."""
        os88marty.guest_sleep(self.m, 1.0)
        self.ui._wait(lambda: not self.byte(STIK_ST) & STIK_S_DIRTY,
                      what, 90, snapshot=lambda: "stik_st = %#x"
                      % self.byte(STIK_ST))
        d = self.raw("SYSTEM/APPDATA/DESKCLOK.DAT")
        if d is None:
            return None
        p = S.parse_dat(d, S.DESKCLOK_PAYLEN, S.DESKCLOK_PAYVER)
        return S.parse_deskclok(p["payload"])

    def clock(self, h, mi, s):
        self.m.write(self.ui._S("clk_sec"), bytes([s, mi, h]))


def win(ui, title):
    """A window by its title, or None - os88ui's window() raises on none."""
    return next((o for o in ui.windows() if o.title == title and o.visible),
                None)


def px(fb, x, y):
    w, _h, d = fb
    i = (y * w + x) * 3
    return tuple(d[i:i + 3])


def region(fb, x0, y0, w, h):
    return [px(fb, x, y) for y in range(y0, y0 + h) for x in range(x0, x0 + w)]


def vga(tmp):
    img = os.path.join(tmp, "os8088-360.img")
    shutil.copy(os.path.join(BDIR, "os8088-360.img"), img)
    with os88ui.boot(img, machine="os8088_xt_vga") as ui:
        c = Clock(ui, tmp)
        m = c.m
        print("A: launched, a desk window, the ground composed")
        w = c.launch()
        check(w.flags & geom.WF_DESK, "the clock is a desk window")
        check(c.word(S.DESKCLOK_BSS_CW) == 128,
              "Medium Classic: a 128-px card (%d)" % c.word(S.DESKCLOK_BSS_CW))
        ui.mo.to(620, 460)
        os88marty.pace(m, 1.0)
        fb = m.fbuf()
        fx, fy = c.face()
        corner, out2 = px(fb, fx, fy + 2), px(fb, fx - 2, fy + 2)
        check(corner == out2, "the dial's corner is the desktop's ground: "
              "the pixel two left of the frame, outside it (%r, %r)"
              % (corner, out2))
        check(corner != px(fb, fx + 1, fy + 2),
              "...the DITHER, phase and all: its neighbour differs (%r %r)"
              % (corner, px(fb, fx + 1, fy + 2)))
        check(px(fb, fx + 64, fy + 1) == (0, 0, 0),
              "the dial's rim is black at 12 (%r)" % (px(fb, fx + 64, fy + 1),))

        print("B: S - the seconds")
        c.activate()
        c.key("KeyS", 1.5)
        ui.mo.to(620, 460)
        os88marty.pace(m, 0.5)
        a = region(m.fbuf(), fx + 16, fy + 16, 96, 80)
        os88marty.guest_sleep(m, 2.2)
        b = region(m.fbuf(), fx + 16, fy + 16, 96, 80)
        check(a != b, "the dial moves from one second to the next")
        check(c.pay()[S.DESKCLOK_FLAGS] & S.DESKCLOK_F_SECS,
              "the payload says seconds")

        print("C: Face > Red LED")
        c.activate()
        ui.menu_pick("Face", "  Red LED")
        os88marty.guest_sleep(m, 4.0)
        ui.mo.to(620, 460)
        os88marty.pace(m, 0.5)
        fb = m.fbuf()
        fx, fy = c.face()
        reds = sum(1 for p in region(fb, fx, fy, 128, 64) if p == RED)
        check(reds > 150, "the face is lit red (%d red pixels)" % reds)
        st = c.saved("the face's save")
        check(st is not None and st["face"] == 6,
              "the file says Red LED (%r)" % (st and st["face_name"],))

        print("D: the settings window, a preview, Cancel and OK")
        c.activate()
        c.key("Enter", 14.0)
        sw = win(ui, "Desk Clock Settings")
        check(sw is not None, "Enter opens the settings window")
        cw = c.find()
        check(sw.x >= cw.x + cw.w or sw.x + sw.w <= cw.x,
              "...beside the clock, not over it (%d..%d, the clock %d..%d)"
              % (sw.x, sw.x + sw.w, cw.x, cw.x + cw.w))
        cx, cy = sw.content[:2]
        ui.mo.click(cx + 248 + 56, cy + 80 + 40, settle=0)     # Neon '90
        os88marty.guest_sleep(m, 8.0)
        check(c.pay()[S.DESKCLOK_FACE] == 2, "the Neon tile is the face now")
        fb = m.fbuf()
        fx, fy = c.face()
        navy = sum(1 for p in region(fb, fx + 24, fy + 24, 64, 48)
                   if p == NAVY)
        check(navy > 400, "...PREVIEWED on the desk: the dial navy (%d)" % navy)
        c.key("Escape", 8.0)
        check(win(ui, "Desk Clock Settings") is None, "Esc closes it")
        check(c.pay()[S.DESKCLOK_FACE] == 6, "...and Cancel puts Red LED back")
        c.activate()
        c.key("Enter", 14.0)
        sw = win(ui, "Desk Clock Settings")
        cx, cy = sw.content[:2]
        ui.mo.click(cx + 8 + 56, cy + 80 + 40, settle=0)       # Classic
        os88marty.guest_sleep(m, 6.0)
        ui.mo.click(cx + 308 + 26, cy + 364 + 9, settle=0)     # OK
        os88marty.guest_sleep(m, 4.0)
        check(win(ui, "Desk Clock Settings") is None, "OK closes it")
        st = c.saved("OK's save")
        check(st is not None and st["face"] == 0,
              "the file says Classic (%r)" % (st and st["face_name"],))

        print("E: F - the face alone")
        h0 = c.find().h
        c.activate()
        c.key("KeyF", 5.0)
        w = c.find()
        check(w.h < h0, "the frame shrinks to the face (%d -> %d)" % (h0, w.h))
        check(c.word(S.DESKCLOK_BSS_CH) == 112,
              "...the card is the dial alone (%d)" % c.word(S.DESKCLOK_BSS_CH))
        ui.mo.to(620, 460)
        os88marty.pace(m, 1.5)
        fb = m.fbuf()
        fx, fy = c.face()
        ty = w.y + w.h - 6                       # the tag row's middle
        a, b, d = px(fb, fx + 40, ty), px(fb, fx + 41, ty), px(fb, fx + 42, ty)
        check(a == d and a != b, "the tag row at rest is the desktop's "
              "dither, no plate (%r %r %r)" % (a, b, d))

        print("F: the alarm rings, and Z snoozes it")
        c.activate()
        c.key("KeyA", 5.0)
        check(win(ui, "Alarm") is not None, "A opens the alarm window")
        for k in ("Space", "Digit0", "Digit7", "Tab", "Digit0", "Digit0"):
            c.key(k, 0.8)
        c.key("Enter", 3.0)
        st = c.saved("the alarm's save")
        check(st is not None and st["alarm"] and (st["ah"], st["am"]) == (7, 0),
              "the file says the alarm is set for 07:00 (%r)" % (st,))
        hq = c.find().h
        c.clock(6, 59, 52)                      # eight seconds to go
        ui._wait(lambda: c.byte(S.DESKCLOK_BSS_RING) == 1, "the alarm to ring",
                 60, snapshot=lambda: "ring %d" % c.byte(S.DESKCLOK_BSS_RING))
        os88marty.guest_sleep(m, 3.0)
        w = c.find()
        check(w.h > hq, "ringing, the face-only frame grows for its rows "
              "(%d -> %d)" % (hq, w.h))
        c.activate()
        c.key("KeyZ", 4.0)
        check(c.byte(S.DESKCLOK_BSS_RING) == 0 and
              c.byte(S.DESKCLOK_BSS_SNZ) == 1, "Z: quiet and snoozed")
        check(c.find().h == hq, "...the frame back to the face (%d)"
              % c.find().h)

        print("G: a reset, and the clock comes back")
        t0 = struct.unpack("<H", bytes(m.read(0x46C, 2)))[0]
        m.reset()
        m.run()
        ui._wait(lambda: struct.unpack("<H", bytes(m.read(0x46C, 2)))[0]
                 < t0, "the BIOS tick to restart", 60)
        ui.ready()
        ui._wait(lambda: c.find() is not None, "the clock's relaunch", 120,
                 snapshot=ui.titles)
        c.bind()
        os88marty.guest_sleep(m, 2.0)
        p = c.pay()
        check(p[S.DESKCLOK_FACE] == 0 and p[S.DESKCLOK_FLAGS]
              & S.DESKCLOK_F_ONLY and p[S.DESKCLOK_FLAGS] & S.DESKCLOK_F_ALARM,
              "relaunched from its row: Classic, face only, the alarm set")


def herc(tmp):
    img = os.path.join(tmp, "os8088-360.img")
    shutil.copy(os.path.join(BDIR, "os8088-360.img"), img)
    with os88ui.boot(img, machine="os8088_5150_herc") as ui:
        c = Clock(ui, tmp)
        m = c.m
        print("H: the Red LED face on 1bpp")
        c.launch()
        c.activate()
        for _ in range(6):
            c.key("ArrowRight", 4.0)
        check(c.pay()[S.DESKCLOK_FACE] == 6, "six faces on: Red LED")
        ui.mo.to(700, 330)
        os88marty.pace(m, 1.0)
        wd, h, rows = m.vram("herc")
        fx, fy = c.face()
        faceh = 64 * 165 // 256
        lit = sum(1 for y in range(fy + 4, fy + faceh - 4)
                  for x in range(fx + 12, fx + 116) if rows[y][x])
        dark = sum(1 for y in range(fy + 4, fy + faceh - 4)
                   for x in range(fx + 12, fx + 116) if not rows[y][x])
        check(lit > 300 and dark > lit,
              "segments lit white on the black face (%d lit, %d dark)"
              % (lit, dark))
        g = [rows[fy + 1][fx + k] for k in range(0, 2)]
        check(g[0] != g[1], "the ground at the face's corner is the "
              "desktop's dither (%r)" % (g,))


def main():
    leg = "vga"
    if "--leg" in sys.argv:
        leg = sys.argv[sys.argv.index("--leg") + 1]
    tmp = tempfile.mkdtemp(prefix="deskclock-")
    try:
        {"vga": vga, "herc": herc}[leg](tmp)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("deskclock %s: %s (%d failure(s))"
          % (leg, "ok" if not fails else "FAILED", len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
