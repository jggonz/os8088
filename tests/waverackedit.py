#!/usr/bin/env python3
"""waverackedit - SPEC.md 123.3, 123.9, 123.10: WaveRack EDITS as the
reference says it must, and SAVES what it shows.

    make && python3 tests/waverackedit.py [--keep]

QEMU BY NAME, for behaviour: the piece table, the history and the BYTES of
what is saved. The B: floppy is built here, on the host: WAVERACK.O88 and
tools/wrsim.py's GOOD fixtures in MEDIA/ - the folder the Standard File
dialog opens on - and a FILL.BIN that leaves about 300 KB free, so a save of
a long document is refused for want of space while every other save fits.

Each edit is made the way a user makes it - a selection DRAGGED in the
waveform, a click for the cursor, the keys X C V Delete T U Y - and after
each one WaveRack's piece table, selection, cursor and history are read by
the package's own map and held to tools/wrsim.py's Editor, fed the selection
the guest really had (a drag's ends are columns; the reference is told the
frames they came to). Then what each save WROTE is read back off the guest's
floppy on the host (a FAT12 reader of this file's own - not the guest's
opinion) and compared byte for byte with wrsim.write_wav() of the same
pieces:

  edits    TONE8.WAV: cut, paste elsewhere, delete, undo, redo, copy, paste
           over a selection, undo twice, redo, trim, undo - the pieces
           coalesced, the history eight deep at most, the title `WaveRack -
           TONE8.WAV*`
  save as  OUT1.WAV byte for byte; no WRSAVE.$$$ left; TONE8.WAV untouched;
           the document named OUT1.WAV and clean; SEL1.WAV (Save Selection
           As) is [s, e) byte for byte
  cancel   STEREO16.WAV cut (the clipboard now names it), Save As begun and
           Escape while it writes: no OUT2.WAV, no WRSAVE.$$$, STEREO16.WAV
           byte-identical, the save's claim given back
  rebase   File > Save over STEREO16.WAV, the file the document reads: the
           file is the edited document byte for byte, the document ONE piece
           of it, the history empty, the clipboard's pieces of the old file
           dropped, `Saved; undo cleared`
  full     STEREO16 pasted into itself until it is bigger than the disk's
           free space: Save As refused before a byte is written (`Not enough
           disk space`), nothing on the disk changed
  pieces   a one-column clip pasted at columns ten apart until the table
           would pass WO_NPC = 64: refused (`Too many edits; save`), the
           table unchanged
  temp     WRSAV00.$$$, a temp file a save KEPT, opened and saved past (Save
           As OUT3.WAV in the same folder): OUT3.WAV byte for byte, the kept
           file byte for byte, the save's own temp another name and gone
           (wave 4's review, H1, M1)
  asked    an edited document's 'Save changes?' up, WaveRack raised under it:
           File > Save greyed, Undo saying 'Answer the question' and
           putting the question back in front, Cancel
           giving the document back (wave 4's review, H2)
  close    the close box on an edited document asks; Discard closes, and no
           claim of the region is left

Not driven: a rename that fails after the target's delete (H1's first path:
nothing here can make OSAPI_FILE_RENAME fail at that instant).

Break on purpose: take the coalescing out of wx_emit and the edits leg fails
at its first paste-back; drop ws_header's pad term and SEL1.WAV's RIFF size
is wrong; let ws_rebase keep the history and the rebase leg fails on [wo_un].
"""
import argparse
import os
import re
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
import waverack as WRT                                        # noqa: E402
import wrsim                                                  # noqa: E402
import xmcheck                                                # noqa: E402
from cycweb import pkg_syms                                   # noqa: E402
from vidxms import Q                                          # noqa: E402

check, say, until, u16, u32 = WRT.check, WRT.say, WRT.until, WRT.u16, WRT.u32
FAIL = WRT.FAIL
FREE_KB = 300                           # what FILL.BIN leaves on the floppy
WS_SIZE = 64                            # wrdoc.inc's source record
TEMP0 = "WRSAV00.$$$"                   # a KEPT temp file on the floppy -
TEMP0_AS = "WRSAV00.XXX"                # os88disk.py takes no '$', so it is
                                        # written as this and its directory
                                        # entry renamed in the image


def fat12_files(path):
    """every file on a FAT12 image, {'DIR/NAME.EXT': bytes} - the root and
    the folders under it (thewire.py's reader)"""
    img = open(path, "rb").read()
    bps, spc, res = u16(img, 11), img[13], u16(img, 14)
    nfat, nroot, spf = img[16], u16(img, 17), u16(img, 22)
    root = (res + nfat * spf) * bps
    data = root + nroot * 32
    fat = img[res * bps:(res + spf) * bps]

    def nxt(c):
        v = u16(fat, c + (c >> 1))
        return (v >> 4) if (c & 1) else (v & 0xFFF)

    def chain(c, size=None):
        blob = b""
        while 2 <= c < 0xFF8 and (size is None or len(blob) < size):
            off = data + (c - 2) * spc * bps
            blob += img[off:off + spc * bps]
            c = nxt(c)
        return blob if size is None else blob[:size]
    out = {}

    def walk(entries, prefix):
        for i in range(0, len(entries) - 31, 32):
            e = entries[i:i + 32]
            if e[0] in (0x00, 0xE5) or e[11] & 0x08:
                continue
            name = e[0:8].decode("latin1").rstrip()
            ext = e[8:11].decode("latin1").rstrip()
            if name in (".", ".."):
                continue
            nm = prefix + name + ("." + ext if ext else "")
            if e[11] & 0x10:
                walk(chain(u16(e, 26)), nm + "/")
                continue
            out[nm.upper()] = chain(u16(e, 26), u32(e, 28))
    walk(img[root:root + nroot * 32], "")
    return out


class ED(WRT.WR):
    """WaveRack on a private QEMU, B: this row's own floppy"""

    def __init__(self, tmp, syms, img, bsrc):
        self.syms, self.img = syms, img
        sysimg = os.path.join(tmp, "sys.img")
        self.bimg = os.path.join(tmp, "b.img")
        shutil.copy(os88build.at("build/os8088.img"), sysimg)
        shutil.copy(bsrc, self.bimg)
        self.q = Q(tmp, sysimg, self.bimg, 4)
        self.sock = self.q.sock
        os88qemu.acted(self.q, lambda: os.path.exists(self.sock), secs=30,
                       what="the QMP socket")
        xmcheck.wait_desktop(self.sock, "waverackedit")
        self.ui = os88ui.UI(self.q, mouse=WRT.Mo(self.sock), sym=xmcheck.sym,
                            verbose=False)
        self.seg = None

    # --- the document, as the guest holds it --------------------------------------
    def pieces(self):
        n = self.w("wo_npc")
        raw = self.rd("wo_pc", n * 10)
        return [struct.unpack_from("<HII", raw, i * 10) for i in range(n)]

    def clip(self):
        n = self.w("wo_ncp")
        raw = self.rd("wo_clip", n * 10)
        return [struct.unpack_from("<HII", raw, i * 10) for i in range(n)]

    def srcname(self, k):
        return self.rd("wo_src", 13, WS_SIZE * k).split(b"\0")[0].decode(
            "latin-1")

    def sel(self):
        return self.d("wo_s"), self.d("wo_e"), self.d("wo_cur")

    def status(self):
        return self.text("wr_stbuf")

    def files(self):
        return fat12_files(self.bimg)

    # --- gestures ---------------------------------------------------------------
    def wpoint(self, col):
        x = self.w("wd_ox") + self.w("we_bx") + col
        y = (self.w("wd_oy") + self.w("we_by") + self.w("we_rulh") +
             self.w("we_lh") // 2)
        return x, y

    def drag(self, c0, c1):
        xa, y = self.wpoint(c0)
        xb, _ = self.wpoint(c1)
        WRT.mouse(self.sock, "to", xa, y)
        time.sleep(0.3)
        WRT.mouse(self.sock, "down", xa, y)
        time.sleep(0.3)
        for x in range(xa, xb + 1, max(1, (xb - xa) // 6)):
            WRT.mouse(self.sock, "to", x, y)
            time.sleep(0.1)
        WRT.mouse(self.sock, "to", xb, y)
        time.sleep(0.3)
        WRT.mouse(self.sock, "up")
        until(lambda: self.d("wo_s") != self.d("wo_e"), "the selection", 5)
        time.sleep(0.3)

    def click(self, col):
        x, y = self.wpoint(col)
        WRT.mouse(self.sock, "to", x, y)
        time.sleep(0.3)
        WRT.mouse(self.sock, "down", x, y)
        time.sleep(0.2)
        WRT.mouse(self.sock, "up")
        time.sleep(0.5)

    def menu(self, mn, item):
        """WR.menu_pick, but the item's name EXACT (File's Save, Save As...
        and Save Selection As... share a prefix)"""
        ui = self.ui
        cells = ui.menus()
        hit = [c for c in cells if c[0].upper().startswith(mn.upper())]
        if len(hit) != 1:
            return False
        _, x0, x1, _ = hit[0]
        cell = cells.index(hit[0])
        WRT.mouse(self.sock, "down", (x0 + x1) // 2, geom.MBAR_H // 2)
        if not until(lambda: ui._word("menu_y1") and ui._byte("menu_dropd"),
                     "the %s menu" % mn, 10):
            WRT.mouse(self.sock, "up")
            return False
        items = ui.menus()[cell][3]
        idx = [k for k, (t, _) in enumerate(items)
               if t.upper().split("  ")[0] == item.upper()]
        if len(idx) != 1 or not items[idx[0]][1]:
            WRT.mouse(self.sock, "up")
            return False
        k = idx[0]
        WRT.mouse(self.sock, "to", ui._word("menu_x1") + 8,
                  ui._word("menu_y1") + 1 + k * geom.MENU_ITEM_H
                  + geom.MENU_ITEM_H // 2)
        ok = until(lambda: ui._word("menu_sel") == k, item, 10)
        WRT.mouse(self.sock, "up")
        time.sleep(0.5)
        return ok

    def alert(self):
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

    def name_in(self, name):
        """the Save dialog: its name box emptied, NAME typed, Enter"""
        if not until(lambda: self.chooser() is not None, "the dialog", 15):
            return False
        time.sleep(0.5)
        for _ in range(14):
            self.key("backspace")
            time.sleep(0.05)
        for ch in name.lower():
            self.key({".": "dot", "$": "shift-4"}.get(ch, ch))
            time.sleep(0.08)
        self.key("ret")
        return until(lambda: self.chooser() is None, "the dialog gone", 15)

    def saved(self, secs=120):
        time.sleep(0.5)
        return until(lambda: self.b("ws_busy") == 0 and self.b("we_busy") == 0,
                     "the save", secs)

    def keyed(self, k):
        self.key(k)
        time.sleep(1.2)
        until(lambda: self.b("we_busy") != 1, "settled", 20)


# =============================================================================
def disk(tmp):
    """this row's B: - the package, the GOOD fixtures in MEDIA/, and a filler
    that leaves FREE_KB free"""
    fx = os.path.join(tmp, "fx")
    subprocess.run([sys.executable, "tools/wrsim.py", "--fixtures", fx],
                   check=True)
    good = sorted(os.listdir(os.path.join(fx, "GOOD")))
    # a temp file a save KEPT (SPEC.md 123.10): a WAV - TONE8.WAV's bytes -
    # named as WaveRack names its temps, for the temp leg to open and save
    # past (wave 4's review, H1)
    shutil.copy(os.path.join(fx, "GOOD", "TONE8.WAV"),
                os.path.join(fx, "GOOD", TEMP0_AS))
    good = sorted(os.listdir(os.path.join(fx, "GOOD")))
    # MEDIA/ with room to SAVE into: the kernel never grows a directory
    # (SPEC.md 18.5), and a folder built to fit its 12 fixtures is ONE
    # cluster, 16 slots - OUT1.WAV and SEL1.WAV filled it, and the rebase's
    # WRSAVE.$$$ was refused `Folder is full` (this row's first run)
    base = [sys.executable, "tools/os88disk.py", "--size", "1440",
            "--dir-slots", "MEDIA=64",
            os88build.at("build/waverack.o88")] + \
        ["MEDIA:%s" % os.path.join(fx, "GOOD", g) for g in good]
    out = os.path.join(tmp, "edit.img")
    r = subprocess.run(base[:2] + ["-o", out] + base[2:], check=True,
                       capture_output=True, text=True)
    used, total = map(int, re.search(r"(\d+)/(\d+) clusters",
                                     r.stdout + r.stderr).groups())
    fill = (total - used - FREE_KB * 2 - 8) * 512
    fp = os.path.join(tmp, "FILL.BIN")
    open(fp, "wb").write(b"\xA5" * fill)
    subprocess.run(base[:2] + ["-o", out] + base[2:] + [fp], check=True,
                   capture_output=True)
    raw = bytearray(open(out, "rb").read())
    at = raw.find(b"WRSAV00 XXX")
    assert at > 0 and raw.find(b"WRSAV00 XXX", at + 1) < 0
    raw[at:at + 11] = b"WRSAV00 $$$"
    open(out, "wb").write(bytes(raw))
    got = dict((g, open(os.path.join(fx, "GOOD", g), "rb").read())
               for g in good)
    got[TEMP0] = got.pop(TEMP0_AS)
    return out, got


def sources(ed, fx):
    """the guest's live source records, as the reference's {index: data}"""
    out = {}
    for k in range(4):
        if ed.q.read((ed.seg << 4) + ed.syms["wo_src"] + WS_SIZE * k + 27,
                     1)[0]:
            name = ed.srcname(k)
            data = fx.get(name)
            if data is not None:
                r = wrsim.parse(data)
                out[k] = data[r["dataoff"]:r["dataoff"] + r["datalen"]]
    return out


def held(ed, ref, what):
    """the guest's document against the reference's, after WHAT"""
    got = (ed.pieces(), ed.sel(), ed.b("wo_un"), ed.b("wo_upos"))
    want = (ref.pieces, (ref.s, ref.e, ref.cur), len(ref.hist), ref.pos)
    return check("%s: pieces %d, sel %d-%d, cursor %d, history %d/%d - the "
                 "reference's" % (what, len(got[0]), got[1][0], got[1][1],
                                  got[1][2], got[3], got[2]), got == want,
                 "got %r want %r" % (got, want))


def step(ed, ref, cmd, key):
    """KEY on the guest, CMD on the reference fed the guest's selection"""
    ref.s, ref.e, ref.cur = ed.sel()
    ref.anc = ed.d("wo_anc")
    r = getattr(ref, cmd)()
    ed.keyed(key)
    held(ed, ref, cmd)
    return r


def leg_edits(ed, fx):
    say(" edits: TONE8.WAV, every edit against wrsim's Editor")
    data = fx["TONE8.WAV"]
    r = wrsim.parse(data)
    fmt = (r["rate"], r["bits"], r["ch"])
    ref = wrsim.Editor([(0, 0, r["frames"])], fmt)
    ed.drag(40, 70)
    step(ed, ref, "cut", "x")
    check("an edit makes it dirty: the title says so",
          ed.text("wr_ttlbuf") == "WaveRack - TONE8.WAV*",
          ed.text("wr_ttlbuf"))
    ed.click(120)
    step(ed, ref, "paste", "v")         # the pasted range is the selection:
    step(ed, ref, "cut", "x")           # cut it again, and paste it back
    ed.click(40)                        # where it came from - ONE piece
    step(ed, ref, "paste", "v")
    check("a cut pasted back where it came from is one piece again",
          len(ed.pieces()) == 1, "%r" % (ed.pieces(),))
    ed.drag(200, 230)
    step(ed, ref, "delete", "delete")
    step(ed, ref, "undo", "u")
    step(ed, ref, "redo", "y")
    ed.drag(15, 25)
    step(ed, ref, "copy", "c")
    ed.drag(300, 340)
    step(ed, ref, "paste", "v")
    step(ed, ref, "undo", "u")
    step(ed, ref, "undo", "u")
    step(ed, ref, "redo", "y")
    ed.drag(100, 400)
    step(ed, ref, "trim", "t")
    step(ed, ref, "undo", "u")
    return ref, fmt


def leg_saves(ed, fx, ref, fmt):
    say(" save as: OUT1.WAV and SEL1.WAV read back off the floppy")
    before = ed.files()
    src = sources(ed, fx)
    want = wrsim.write_wav(ref.pieces, src, fmt)
    seen = {}
    ed.menu("File", "Save As...")
    ed.name_in("OUT1.WAV")
    until(lambda: ed.b("ws_busy") == 1 or ed.text("wr_stbuf").startswith(
        "Saved"), "saving", 10, 0.02)
    if ed.b("ws_busy"):
        seen["heap"] = ed.heap()
    ed.saved()
    fs = ed.files()
    check("OUT1.WAV is wrsim.write_wav of the pieces, byte for byte (%d)"
          % len(want), fs.get("MEDIA/OUT1.WAV") == want,
          "%r bytes" % (len(fs.get("MEDIA/OUT1.WAV") or b""),))
    check("no temp file left", not any("WRSAV" in k and TEMP0 not in k
                                       for k in fs))
    check("TONE8.WAV untouched", fs.get("MEDIA/TONE8.WAV") ==
          before["MEDIA/TONE8.WAV"])
    check("the document is OUT1.WAV and clean (%r)" % ed.text("wr_ttlbuf"),
          ed.text("wo_name") == "OUT1.WAV" and ed.b("wo_dirty") == 0 and
          ed.text("wr_ttlbuf") == "WaveRack - OUT1.WAV")
    check("'Saved OUT1.WAV'", ed.status() == "Saved OUT1.WAV", ed.status())
    if "heap" in seen:
        say("     heap, saving: %s KB = %d KB" % (seen["heap"],
                                                 sum(seen["heap"])))
    ed.drag(50, 90)
    s, e, _ = ed.sel()
    want = wrsim.write_wav(ref.pieces, src, fmt, s, e)
    ed.menu("File", "Save Selection As...")
    ed.name_in("SEL1.WAV")
    ed.saved()
    fs = ed.files()
    check("SEL1.WAV is [%d, %d) byte for byte" % (s, e),
          fs.get("MEDIA/SEL1.WAV") == want)
    check("the document is still OUT1.WAV", ed.text("wo_name") == "OUT1.WAV")


def leg_cancel(ed, fx):
    say(" cancel: a Save As of STEREO16 stopped with Escape")
    before = ed.files()
    if not check("STEREO16.WAV opens", ed.open("MEDIA/STEREO16.WAV")):
        return None
    ed.settled()
    ed.drag(100, 180)
    ed.keyed("x")
    check("the clipboard names STEREO16.WAV", any(
        ed.srcname(p[0]) == "STEREO16.WAV" for p in ed.clip()))
    pieces = ed.pieces()
    ed.menu("File", "Save As...")
    ed.name_in("OUT2.WAV")
    until(lambda: ed.b("ws_busy") == 1, "saving", 10, 0.02)
    ed.key("esc")
    heap = ed.heap()
    until(lambda: ed.b("ws_busy") == 0, "cancelled", 10)
    time.sleep(0.5)
    fs = ed.files()
    check("'Save cancelled'", ed.status() == "Save cancelled", ed.status())
    check("no OUT2.WAV, no WRSAVE.$$$", "MEDIA/OUT2.WAV" not in fs and
          not any("WRSAVE" in k for k in fs))
    check("STEREO16.WAV byte-identical", fs.get("MEDIA/STEREO16.WAV") ==
          before["MEDIA/STEREO16.WAV"])
    check("the save's claim given back, the document still edited",
          ed.w("ws_oseg") == 0 and ed.b("wo_dirty") == 1)
    say("     heap, saving (sampled before the Escape landed): %s KB = %d KB"
        % (heap, sum(heap)))
    return pieces


def leg_rebase(ed, fx, pieces):
    say(" rebase: File > Save over STEREO16.WAV, the file it reads")
    data = fx["STEREO16.WAV"]
    r = wrsim.parse(data)
    fmt = (r["rate"], r["bits"], r["ch"])
    want = wrsim.write_wav(pieces, sources(ed, fx), fmt)
    frames = ed.d("wo_frames")
    ed.menu("File", "Save")
    ed.saved()
    fs = ed.files()
    check("STEREO16.WAV is the edited document byte for byte",
          fs.get("MEDIA/STEREO16.WAV") == want)
    p = ed.pieces()
    check("the document is ONE piece of the saved file (%r)" % (p,),
          len(p) == 1 and p[0][1:] == (0, frames) and
          ed.srcname(p[0][0]) == "STEREO16.WAV" and
          ed.q.read((ed.seg << 4) + ed.syms["wo_src"] + 40 * p[0][0] + 16,
                    4) == struct.pack("<I", 44))
    check("the history is empty, its claim given back",
          ed.b("wo_un") == 0 and ed.w("wo_useg") == 0)
    check("the clipboard's pieces of the old file dropped",
          ed.w("wo_ncp") == 0)
    check("'Saved; undo cleared'", ed.status() == "Saved; undo cleared",
          ed.status())
    check("clean: 'WaveRack - STEREO16.WAV'",
          ed.text("wr_ttlbuf") == "WaveRack - STEREO16.WAV")
    fx["STEREO16.WAV"] = want           # the file the disk holds now


def leg_full(ed, fx):
    say(" full: a document bigger than the disk's free space")
    before = ed.files()
    ed.key("a")
    time.sleep(0.3)
    ed.keyed("c")
    while ed.d("wo_frames") * 4 + 44 < (FREE_KB + 64) * 1024:
        ed.key("end")
        time.sleep(0.3)
        ed.keyed("v")
    ed.menu("File", "Save As...")
    ed.name_in("BIG.WAV")
    time.sleep(1)
    check("refused before writing: 'Not enough disk space' (%d bytes)" %
          (ed.d("wo_frames") * 4), ed.status() == "Not enough disk space",
          ed.status())
    check("nothing on the disk changed", ed.files() == before and
          ed.b("ws_busy") == 0)


def leg_pieces(ed):
    say(" pieces: a one-column clip pasted until WO_NPC would pass")
    ed.menu("File", "Open...")
    until(lambda: ed.alert() is not None or ed.chooser() is not None,
          "the question", 10)
    if ed.alert():                      # the edited document: discarded
        ed.alert_btn(1)
    until(lambda: ed.chooser() is not None, "the dialog", 15)
    time.sleep(0.3)
    g = ed.w("wo_gen")
    ed.chooser_open("MEDIA/TONE8.WAV")
    until(lambda: ed.w("wo_gen") != g and ed.b("we_busy") == 0, "TONE8", 60)
    ed.drag(5, 5 + 1)
    ed.keyed("c")
    n, col = 0, 20
    while n < 40:
        before = ed.pieces()
        ed.click(col)
        ed.keyed("v")
        n += 1
        col += 10
        if ed.status() != "Pasted":
            break
    check("refused at %d pieces: %r" % (len(before), ed.status()),
          ed.status() == "Too many edits; save" and ed.pieces() == before and
          len(before) <= wrsim.WO_NPC, "after %d pastes" % n)


def leg_temp(ed, fx):
    """WAVE 4'S REVIEW, H1: a temp file a save kept, opened and saved past.
    Wave 4's fixed WRSAVE.$$$ was CREATED OVER whatever had the name - the
    document's own source among them. A temp is now WRSAVnn.$$$, the first
    name that is not there: TEMP0 opened, Save As OUT3.WAV in the same
    folder, and TEMP0 must come through byte for byte"""
    say(" temp: %s (a kept save) opened, Save As OUT3.WAV beside it" % TEMP0)
    before = ed.files()
    ed.menu("File", "Open...")
    until(lambda: ed.alert() is not None or ed.chooser() is not None,
          "the question", 10)
    if ed.alert():                      # the edited document: discarded
        ed.alert_btn(1)
    until(lambda: ed.chooser() is not None, "the dialog", 15)
    time.sleep(0.3)
    g = ed.w("wo_gen")
    ed.chooser_open("MEDIA/" + TEMP0)
    until(lambda: ed.w("wo_gen") != g and ed.b("we_busy") == 0, TEMP0, 60)
    check("%s opens (%d frames, %r)" % (TEMP0, ed.d("wo_frames"),
                                        ed.text("wo_name")),
          ed.d("wo_frames") == 11025 and ed.text("wo_name") == TEMP0)
    ed.menu("File", "Save As...")
    ed.name_in("OUT3.WAV")
    ed.saved()
    fs = ed.files()
    data = fx["TONE8.WAV"]
    r = wrsim.parse(data)
    want = wrsim.write_wav([(0, 0, r["frames"])],
                           {0: data[r["dataoff"]:r["dataoff"] + r["datalen"]]},
                           (11025, 8, 1))
    check("OUT3.WAV is the document byte for byte (%d)" % len(want),
          fs.get("MEDIA/OUT3.WAV") == want)
    check("%s came through byte for byte (the save took %r)"
          % (TEMP0, ed.text("ws_tmp")),
          fs.get("MEDIA/" + TEMP0) == before.get("MEDIA/" + TEMP0) and
          ed.text("ws_tmp") != TEMP0)
    check("no other temp file left", not any(
        "WRSAV" in k and TEMP0 not in k for k in fs), repr(sorted(fs)))


def leg_asked(ed):
    """WAVE 4'S REVIEW, H2: the alerts are not modal, so while 'Save changes
    to NAME?' is up the document's commands GREY - File > Save refused, Undo
    saying `Answer the question` - and Cancel gives them back"""
    say(" asked: the document's commands while the question is up")
    ed.menu("File", "Open...")
    asked = until(lambda: ed.alert() is not None, "the question", 10)
    check("an edited document: 'Save changes to ...?' asked", asked,
          ed.text("wr_qbuf"))
    if not asked:
        return
    w = ed.window()                     # back to WaveRack, the alert up
    xmcheck.click(ed.sock, w.x + w.w // 2, w.y + 6)
    time.sleep(0.8)
    check("File > Save is greyed under the question",
          not ed.menu("File", "Save"))
    before = (ed.pieces(), ed.b("wo_upos"))
    ed.key("u")                         # Undo: live a moment ago
    time.sleep(0.8)
    check("Undo says 'Answer the question' and changes nothing (%r)"
          % ed.status(), ed.status() == "Answer the question" and
          (ed.pieces(), ed.b("wo_upos")) == before and
          ed.b("ws_busy") == 0)
    front = ed.ui.front()
    a = ed.alert()
    check("...and the refusal put the question back in front", a is not None
          and front is not None and front.i == a.i)
    if a:
        ed.alert_btn(2)                 # Cancel
    check("Cancel: the question gone, the document kept (%r)"
          % ed.text("wo_name"), ed.b("wr_qact") == 0 and
          ed.b("wo_dirty") == 1)


def leg_close(ed):
    say(" close: the close box on an edited document")
    w = ed.window()
    cx, cy = geom.close_xy(w.x, w.y)
    xmcheck.click(ed.sock, cx, cy)
    asked = until(lambda: ed.alert() is not None, "the question", 10)
    check("'Save changes to TONE8.WAV?' asked", asked and
          ed.text("wr_qbuf") == "Save changes to TONE8.WAV?",
          ed.text("wr_qbuf"))
    if asked:
        ed.alert_btn(1)                 # Discard
    check("Discard closes the window", until(lambda: ed.wrseg()[0] is None,
                                             "the close", 15))
    time.sleep(1)
    left = ed.heap()
    check("no claim of the closed region left (%s)" % left, left == [])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true",
                    help="leave the scratch folder behind")
    a = ap.parse_args()
    for p in ("build/os8088.img", "build/waverack.o88", "build/waverack.bin"):
        if not os.path.exists(os88build.at(p)):
            sys.exit("waverackedit: no %s - make" % p)
    syms, img = pkg_syms("apps/waverack/waverack.asm",
                         ("apps/", "apps/waverack/", os88build.at("build") + "/"))
    if img != open(os88build.at("build/waverack.bin"), "rb").read():
        sys.exit("waverackedit: build/waverack.bin is not this source's - make")
    tmp = tempfile.mkdtemp(dir=os.path.join(ROOT, "build"), prefix="wre")
    ed = None
    try:
        bimg, fx = disk(tmp)
        ed = ED(tmp, syms, img, bimg)
        if not check("WaveRack launches off B:", ed.launch()):
            return 1
        time.sleep(1)
        if not check("TONE8.WAV opens", ed.open("MEDIA/TONE8.WAV")):
            return 1
        ed.settled()
        say("     heap, idle with it open: %s KB" % ed.heap())
        ref, fmt = leg_edits(ed, fx)
        say("     heap, idle with a history: %s KB" % ed.heap())
        leg_saves(ed, fx, ref, fmt)
        pieces = leg_cancel(ed, fx)
        if pieces is not None:
            leg_rebase(ed, fx, pieces)
            leg_full(ed, fx)
        leg_temp(ed, fx)
        leg_pieces(ed)
        leg_asked(ed)
        leg_close(ed)
    finally:
        if ed is not None:
            try:
                ed.q.close()
            except Exception:                               # noqa: BLE001
                pass
        if a.keep:
            say("waverackedit: kept %s" % tmp)
        else:
            shutil.rmtree(tmp, ignore_errors=True)
    for f in FAIL:
        say("  FAIL: " + f)
    say("waverackedit: %s" % ("FAILED (%d)" % len(FAIL) if FAIL else "ok"))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
