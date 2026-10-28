#!/usr/bin/env python3
"""waverack - SPEC.md 123.3-123.5: WaveRack opens every fixture as the
reference says it must.

    make && make waverackfix && python3 tests/waverack.py [--keep]

QEMU BY NAME: what is asserted is behaviour - the parse, the envelope, the
view cache - and not speed. The B: floppy is build/wrfix.img (`make
waverackfix`): WAVERACK.O88 at the root and tools/wrsim.py's fixtures in
MEDIA/ - GOOD/ the files it must open, BAD/ the ones it must refuse, and
LONG.WAV, 1.1 MB, which only a scan that STREAMS can take.

Every fixture is opened the way a user opens one - File > Open..., the
Standard File dialog's own rows double-clicked - and then WaveRack's bss is
read by its own map (nasm's, checked to be the bytes of build/waverack.bin)
and held to tools/wrsim.py's parse() of the same bytes:

  accepted  the format (rate, bits, channels), the frame count, the data
            offset, the note ([ww_note]: clamped, floored, recovered) and the
            dirty flag; the status bar's sentence, word for word; and the
            VIEW CACHE at Fit, every column of every channel, against
            wrsim.columns() - which is the impulses being there at full zoom
            out, the stereo spike on R alone, and LONG.WAV's 1,123 blocks
  refused   [ww_err] and the number its sentence names ([ww_arg]), the
            sentence on the status bar, and the document EMPTY
  zoomed    on IMPULSE.WAV, the cursor walked to a spike and `+` pressed to
            fpp = 1: every column refined from the samples, each span
            reaching the previous column's last sample, equal to the
            reference column for column
  identity  after the zoom, the window's pixels, then a forced full repaint
            (the About card up and down) - the same pixels (retro-face's
            Step 6: incremental and full draws are one code path)

Break on purpose: drop the pad byte in ww_parse's `.next` and LISTODD.WAV is
refused (No data chunk); take continuity out of we_rnext and the zoomed leg
fails at the columns either side of each spike.
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tests"), os.path.join(ROOT, "tools")]
os.chdir(ROOT)
import os88build                                              # noqa: E402
import os88geom as geom                                       # noqa: E402
import os88qemu                                               # noqa: E402
import os88ui                                                 # noqa: E402
import wrsim                                                  # noqa: E402
import xmcheck                                                # noqa: E402
from cycweb import pkg_syms                                   # noqa: E402
from vidxms import Q                                          # noqa: E402

FAIL = []
WS_DOFF, WS_SIZE = 16, 64               # wrdoc.inc's source record
WE_EXACT = 640 * 4                      # wrenv.inc's cache layout
BANDKB = None                           # WRL_BANDKB: the band, from entry


def say(s):
    print(s, flush=True)


def check(what, ok, more=""):
    say("   %-60s %s%s" % (what, "ok" if ok else "FAIL",
                           "" if ok else "  " + more))
    if not ok:
        FAIL.append(what)
    return ok


def u16(b, o=0):
    return struct.unpack_from("<H", b, o)[0]


def u32(b, o=0):
    return struct.unpack_from("<I", b, o)[0]


class Mo(object):
    """os88ui's mouse, over QMP (tools/mouse.py, xmcheck's double-click)"""

    def __init__(self, sock):
        self.sock = sock

    def click(self, x, y, settle=0):
        xmcheck.click(self.sock, x, y)

    def dblclick(self, x, y, settle=0):
        xmcheck.dblclick(self.sock, x, y)


def mouse(sock, *a):
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "mouse.py"),
                    sock] + [str(x) for x in a], capture_output=True, cwd=ROOT)


def until(cond, what, secs=30.0, poll=0.1):
    t = time.time()
    while time.time() - t < secs:
        try:
            if cond():
                return True
        except Exception:                                   # noqa: BLE001
            pass
        time.sleep(poll)
    say("     (timed out waiting for %s)" % what)
    return False


class WR(object):
    """WaveRack on a private QEMU"""

    def __init__(self, tmp, syms, img):
        self.syms, self.img = syms, img
        sysimg = os.path.join(tmp, "sys.img")
        bimg = os.path.join(tmp, "b.img")
        shutil.copy(os88build.at("build/os8088.img"), sysimg)
        shutil.copy(os88build.at("build/wrfix.img"), bimg)
        self.q = Q(tmp, sysimg, bimg, 4)
        self.sock = self.q.sock
        os88qemu.acted(self.q, lambda: os.path.exists(self.sock), secs=30,
                       what="the QMP socket")
        xmcheck.wait_desktop(self.sock, "waverack")
        self.ui = os88ui.UI(self.q, mouse=Mo(self.sock), sym=xmcheck.sym,
                            verbose=False)
        self.seg = None

    # --- the guest ------------------------------------------------------------
    def rd(self, name, n=1, off=0):
        return self.q.read((self.seg << 4) + self.syms[name] + off, n)

    def b(self, name, off=0):
        return self.rd(name, 1, off)[0]

    def w(self, name, off=0):
        return u16(self.rd(name, 2, off))

    def d(self, name, off=0):
        return u32(self.rd(name, 4, off))

    def text(self, name, n=40):
        return self.rd(name, n).split(b"\0")[0].decode("latin-1")

    def key(self, k):
        xmcheck.qmp(self.sock, "sendkey " + k)

    def wrseg(self):
        n = xmcheck.sym("wm_wins")
        for i in range(geom.MAX_WIN):
            rec = self.q.read(n + i * geom.WIN_SIZE, geom.WIN_SIZE)
            seg = u16(rec, geom.W_SEG)
            if seg and u16(rec, geom.W_FLAGS) & 2:
                if self.q.read((seg << 4) + 16, 8) == b"WaveRack":
                    return seg, i
        return None, None

    def heap(self):
        """WaveRack's claims on the kernel's map (SPEC.md 50.3), as KB each:
        a package's claims are owned by its region's SEGMENT (the region
        itself by its instance slot, and is not counted)"""
        n = 32                                  # MEM_MAX (tests/heapcheck.py)
        raw = self.q.read(xmcheck.sym("mem_tab"), n * geom.MC_SIZE)
        recs = [tuple(u16(raw, i * geom.MC_SIZE + k) for k in (0, 2, 4))
                for i in range(n)]
        return sorted((r[1] * 16 + 1023) // 1024 for r in recs
                      if r[0] and r[2] == self.seg)

    def window(self):
        _, i = self.wrseg()
        return next(o for o in self.ui.windows() if o.i == i)

    # --- launching, menus, the dialog -------------------------------------------
    def launch(self):
        x, y = geom.drive_pt(self.q, "B", xmcheck.sym)
        xmcheck.dblclick(self.sock, x, y)
        box = {}

        def disk():
            for w in self.ui.windows():
                if w.visible and self.ui.fs_of(w) == (1, 0, 1):
                    box["w"] = w
                    return True
            return False
        if not until(disk, "Disk B"):
            return False
        time.sleep(1)
        w = box["w"]
        i, _ = self.ui.entry("WAVERACK.O88", w)
        for _ in range(3):
            xmcheck.dblclick(self.sock, *self.ui.row_xy(w, i - self.ui.scroll(w)))
            if until(lambda: self.wrseg()[0] is not None, "WaveRack", 20):
                self.seg = self.wrseg()[0]
                # the bss is read by THIS source's map: the package running
                # must be build/waverack.bin's bytes, not a stale floppy's
                # (its CODE: the data after wr_tpl is written as it runs -
                # the menus' greying, the drop-downs, the key record)
                end = self.syms["wr_tpl"]
                got = self.q.read((self.seg << 4) + 64, end - 64)
                return check("the running WaveRack is build/waverack.bin "
                             "(make waverackfix)", got == self.img[64:end])
        return False

    def menu_pick(self, menu, item):
        """press on the title, confirm the pull-down, move to the item,
        confirm it is the highlighted one, release (rdxms's gesture)"""
        ui = self.ui
        cells = ui.menus()
        hit = [c for c in cells if c[0].upper().startswith(menu.upper())]
        if len(hit) != 1:
            return False
        _, x0, x1, _ = hit[0]
        cell = cells.index(hit[0])
        mouse(self.sock, "down", (x0 + x1) // 2, geom.MBAR_H // 2)
        if not until(lambda: ui._word("menu_y1") and ui._byte("menu_dropd"),
                     "the %s menu" % menu, 10):
            mouse(self.sock, "up")
            return False
        items = ui.menus()[cell][3]
        idx = [k for k, (t, _) in enumerate(items)
               if t.upper().startswith(item.upper())]
        if len(idx) != 1 or not items[idx[0]][1]:
            mouse(self.sock, "up")
            return False
        k = idx[0]
        mouse(self.sock, "to", ui._word("menu_x1") + 8,
              ui._word("menu_y1") + 1 + k * geom.MENU_ITEM_H
              + geom.MENU_ITEM_H // 2)
        ok = until(lambda: ui._word("menu_sel") == k, item, 10)
        mouse(self.sock, "up")
        return ok

    def chooser(self):
        ptr = self.ui._word("fdlg_win")
        if ptr in (0, 0xFFFF):
            return None
        i = (ptr - xmcheck.sym("wm_wins") + (geom.KERNEL_SEG << 4)) \
            // geom.WIN_SIZE
        return next((o for o in self.ui.windows() if o.i == i and o.visible),
                    None)

    def names(self, w):
        return [r[0].upper() for r in self.ui.listing(w)]

    def chooser_open(self, path):
        """walk the chooser to path's file: a folder or `..` at a time, as
        the listing it shows allows, then the file - every row a real
        double-click on the row the chooser drew"""
        want = path.upper().split("/")
        for _ in range(8):
            w = self.chooser()
            if w is None:
                return False
            names = self.names(w)
            target = None
            for k, part in enumerate(want):
                if part in names:
                    target = part
                    rest = want[k + 1:]
            if target is None:
                target, rest = "..", want
            self.show_row(w, target)
            w = self.chooser()
            i, _ = self.ui.entry(target, w)
            before = names
            xmcheck.dblclick(self.sock, *self.ui.row_xy(w, i - self.ui.scroll(w)))
            if not rest:
                return until(lambda: self.chooser() is None, "the dialog", 15)
            until(lambda: self.chooser() is None or
                  self.names(self.chooser()) != before, "the folder", 10)
            want = rest if target != ".." else want
        return False

    def show_row(self, w, name):
        """scroll the chooser until NAME's row is on the glass (the arrows,
        reading the view's first row back after each)"""
        i, _ = self.ui.entry(name, w)
        rows = (w.h - geom.TITLE_H - 2 - geom.FM_ROW_Y0 - 16) // geom.FM_ROW_H
        for _ in range(64):
            top = self.ui.scroll(w)
            if top <= i < top + max(1, rows):
                return
            self.key("up" if i < top else "down")
            until(lambda: self.ui.scroll(w) != top, "the scroll", 2)

    def alert(self):
        """os88ui's alert window (a question), or None"""
        ptr = self.w("os88ui_awin")
        if not ptr:
            return None
        i = (ptr - xmcheck.sym("wm_wins") + (geom.KERNEL_SEG << 4)) \
            // geom.WIN_SIZE
        return next((o for o in self.ui.windows() if o.i == i), None)

    def alert_btn(self, k, n=3):
        """button K of the alert (os88ui's row: n buttons of 72 + 12)"""
        w = self.alert()
        row = n * 84 - 12
        xmcheck.click(self.sock, w.x + (w.w - row) // 2 + 84 * k + 36,
                      w.y + 18 + 46 + 6)
        until(lambda: not self.w("os88ui_awin"), "the alert answered", 10)
        time.sleep(0.5)

    def open(self, path):
        g = self.w("wo_gen")
        if not self.menu_pick("File", "Open"):
            return False
        # AN EDITED DOCUMENT IS ASKED ABOUT FIRST (wave 4, SPEC.md 123.3) -
        # and a recovered take opens dirty (123.4), so after UNFIN.WAV the
        # next Open raised 'Save changes to UNFIN.WAV?' and this waited for
        # a dialog that the question stood in front of: Discard it
        if not until(lambda: self.chooser() is not None or
                     self.alert() is not None, "the dialog", 15):
            return False
        if self.chooser() is None and self.alert() is not None:
            self.alert_btn(1)               # Discard
        if not until(lambda: self.chooser() is not None, "the dialog", 15):
            return False
        time.sleep(0.3)
        if not self.chooser_open(path):
            return False
        return until(lambda: self.w("wo_gen") != g and self.b("we_busy") == 0,
                     "the open and its scan", 120)

    def settled(self, secs=60):
        return until(lambda: self.b("we_busy") == 0, "the refinement", secs)

    def shot(self, path, w=None):
        w = w or self.window()
        mouse(self.sock, "to", 634, 474)    # the pointer is drawn into the
        time.sleep(0.5)                     # framebuffer: off the window
        subprocess.run([sys.executable, os.path.join(ROOT, "tools", "shot.py"),
                        self.sock, path, "--crop",
                        "%d,%d,%d,%d" % (w.x, w.y, w.w, w.h)],
                       capture_output=True, cwd=ROOT)

    def cache(self):
        """the view cache: per column [(min, max)] per channel (None: empty),
        and the exact bytes"""
        seg = self.w("we_vseg")
        ww, ch = self.w("we_ww"), self.b("wo_ch")
        raw = self.q.read(seg << 4, 640 * 4 + 640)
        cols, exact = [], []
        for c in range(ww):
            e = raw[c * 4:c * 4 + 4]
            v = [x - 256 if x > 127 else x for x in e]
            if v[0] > v[1]:
                cols.append(None)
            else:
                cols.append([(v[0], v[1])] + ([(v[2], v[3])] if ch == 2 else []))
            exact.append(raw[WE_EXACT + c])
        return cols, exact


def compare_cache(wr, data, r, what):
    """the view cache against wrsim.columns() for the view WaveRack shows"""
    ch = wrsim.samples(data, r)
    env, s = wrsim.envelope(ch, r["frames"])
    first, z, ww = wr.d("wv_first"), wr.b("wv_z"), wr.w("we_ww")
    ref = wrsim.columns(ch, env, s, r["frames"], first, z, ww)
    got, exact = wr.cache()
    bad = [c for c in range(ww) if got[c] != ref[c]]
    check("%s: z %d first %d - the view cache is the reference's" % (what, z,
                                                                    first),
          not bad, "%d columns differ, first %s: got %s want %s"
          % (len(bad), bad[:1], [got[c] for c in bad[:1]],
             [ref[c] for c in bad[:1]]))
    check("%s: every column exact" % what, all(exact), "%d not"
          % exact.count(0))
    return ref


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true",
                    help="leave the scratch folder (screenshots) behind")
    a = ap.parse_args()
    for p in ("build/os8088.img", "build/wrfix.img", "build/waverack.bin"):
        if not os.path.exists(os88build.at(p)):
            sys.exit("waverack: no %s - make && make waverackfix" % p)
    syms, img = pkg_syms("apps/waverack/waverack.asm",
                         ("apps/", "apps/waverack/", os88build.at("build") + "/"))
    if img != open(os88build.at("build/waverack.bin"), "rb").read():
        sys.exit("waverack: build/waverack.bin is not this source's - make")
    global BANDKB
    for ln in open(os88build.at("build/wrart.inc")):
        if ln.startswith("WRL_BANDKB"):
            BANDKB = int(ln.split()[2])
    fx = wrsim.fixtures()
    tmp = tempfile.mkdtemp(dir=os.path.join(ROOT, "build"), prefix="wr")
    try:
        wr = WR(tmp, syms, img)
        if not check("WaveRack launches off B:", wr.launch()):
            return 1
        time.sleep(1)
        order = sorted(n for n in fx if fx[n][1] == "GOOD") + \
            sorted(n for n in fx if fx[n][1] == "BAD") + ["LONG.WAV"]
        say("waverack: %d fixtures, each through File > Open" % len(order))
        for name in order:
            data, folder, why = fx[name]
            r = wrsim.parse(data)
            path = ("MEDIA/%s/%s" % (folder, name)) if folder else \
                "MEDIA/" + name
            say(" %s - %s" % (name, why))
            if not check("%s: opened through the dialog" % name,
                         wr.open(path)):
                continue
            wr.settled()
            err, arg = wr.b("ww_err"), wr.w("ww_arg")
            stat = wr.text("wr_stbuf")
            want = wrsim.sentence(r) or "Ready"
            if r["err"]:
                check("%s: refused with code %d (%d), the reference's" %
                      (name, err, arg), (err, arg) == (r["err"], r["arg"]),
                      "want %d (%d)" % (r["err"], r["arg"]))
                check("%s: the document is empty" % name,
                      wr.b("wo_doc") == 0)
                check("%s: says %r" % (name, want), stat == want,
                      "said %r" % stat)
                h = wr.heap()
                check("%s: every document claim given back (heap %s KB)" %
                      (name, h), h == [BANDKB], "want only the band")
                continue
            src = wr.syms["wo_src"]
            got = dict(rate=wr.w("wo_rate"), bits=wr.b("wo_bits"),
                       ch=wr.b("wo_ch"), frames=wr.d("wo_frames"),
                       dataoff=u32(wr.q.read((wr.seg << 4) + src + WS_DOFF, 4)),
                       note=wr.b("ww_note"), dirty=bool(wr.b("wo_dirty")))
            ref = dict((k, r[k]) for k in got)
            check("%s: %d Hz %d-bit %dch, %d frames at %d (note %d)" %
                  (name, got["rate"], got["bits"], got["ch"], got["frames"],
                   got["dataoff"], got["note"]), got == ref,
                  "want %r" % ref)
            check("%s: err 0, a document" % name,
                  err == 0 and wr.b("wo_doc") == 1)
            check("%s: says %r" % (name, want), stat == want,
                  "said %r" % stat)
            compare_cache(wr, data, r, name)
            h = wr.heap()
            say("     heap, idle with it open: %s KB = %d KB" % (h, sum(h or [])))
            if name == "IMPULSE.WAV":
                leg_zoom(wr, data, r, tmp)
    finally:
        try:
            wr.q.close()
        except Exception:                                   # noqa: BLE001
            pass
        if a.keep:
            say("waverack: kept %s" % tmp)
        else:
            shutil.rmtree(tmp, ignore_errors=True)
    for f in FAIL:
        say("  FAIL: " + f)
    say("waverack: %s" % ("FAILED (%d)" % len(FAIL) if FAIL else "ok"))
    return 1 if FAIL else 0


def leg_zoom(wr, data, r, tmp):
    """the cursor to the second spike, `+` to fpp = 1, the samples refined,
    and the identity check"""
    fr = wrsim.IMPULSES[1][0]
    z = wr.b("wv_z")
    for _ in range(fr >> z):            # Right: one column a press
        wr.key("right")
    until(lambda: wr.d("wo_cur") == (fr >> z) << z, "the cursor", 20)
    for _ in range(z):
        wr.key("kp_add")
        time.sleep(0.3)
    until(lambda: wr.b("wv_z") == 0, "fpp = 1", 20)
    wr.settled()
    check("IMPULSE: zoomed to fpp 1 about the cursor", wr.b("wv_z") == 0,
          "z %d" % wr.b("wv_z"))
    ref = compare_cache(wr, data, r, "IMPULSE zoomed")
    first = wr.d("wv_first")
    c = fr - first
    if 0 < c < len(ref) - 1:
        check("IMPULSE zoomed: the spike's column and the next reach it",
              ref[c][0][0] == -128 and ref[c + 1][0][0] == -128,
              "%r %r" % (ref[c], ref[c + 1]))
    time.sleep(0.5)
    a = os.path.join(tmp, "inc.png")
    b = os.path.join(tmp, "full.png")
    wr.shot(a)
    wr.menu_pick("WaveRack", "About")
    time.sleep(1)
    w = wr.window()
    xmcheck.click(wr.sock, w.x + w.w // 2, w.y + w.h // 2)
    time.sleep(1.5)
    wr.shot(b)
    try:
        from PIL import Image, ImageChops
        d = ImageChops.difference(Image.open(a).convert("RGB"),
                                  Image.open(b).convert("RGB")).getbbox()
        check("identity: incremental == a full repaint (zoomed)", d is None,
              "differ in %r" % (d,))
    except ImportError:
        say("   (no PIL: the identity check is not taken)")


if __name__ == "__main__":
    sys.exit(main())
