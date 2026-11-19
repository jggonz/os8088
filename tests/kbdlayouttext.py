#!/usr/bin/env python3
"""Native keyboard/text integration on one VGA, CGA or Hercules machine.

Physical keys enter both Spanish layouts and French CP850 into Note Pad;
edits, Find/Replace and an independent FAT read check the actual bytes.
Two live instances must retain their own glyph holds after selecting BIOS
default. Their extended glyphs must survive a forced repaint, save/reopen,
New and the other instance closing; the final New must drop KEYBOARD.DRV.

This proposed soak row is not an implementation-time gate. --evidence DIR
writes rendered page/note screenshots. --negative-release deliberately
disables the module's RELEASE body after the positive typing/save checks;
the ownership assertions must then fail. No fixture is built by this row.
"""
import argparse
import os
from pathlib import Path
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import os88build                                                # noqa: E402
import os88flush                                                # noqa: E402
import os88marty                                                # noqa: E402
import os88sym                                                  # noqa: E402
import os88ui                                                   # noqa: E402
from notepad import state as npstate                            # noqa: E402
from kbdsettings import KeyboardPage, require, set_file           # noqa: E402


# Authored test vectors, deliberately independent of keyboardmaps.py and its
# generated tables. Quote is Spain's acute dead key; Latin America uses [.
ES = b"es \xa4 \xa0 \xad\r"       # es enie, a-acute, inverted exclamation
LA = b"la \xa4 \xa0 \xa8\r"       # la enie, a-acute, inverted question
FR = b"fr a \x82 \x88 \x85"        # fr a, e-acute, e-circumflex, a-grave
KEYS = (
    (2, ("KeyE", "KeyS", "Space", "Semicolon", "Space", "Quote",
         "KeyA", "Space", "Equal", "Enter"), ES),
    (3, ("KeyL", "KeyA", "Space", "Semicolon", "Space", "BracketLeft",
         "KeyA", "Space", "Equal", "Enter"), LA),
    (4, ("KeyF", "KeyR", "Space", "KeyQ", "Space", "Digit2", "Space",
         "BracketLeft", "KeyE", "Space", "Digit0"), FR),
)
EXPECTED = ES + LA + FR
REPLACED = ES + LA + b"fr a \x88 \x88 \x85"


class Note:
    """State offsets from the shipped raw binary, every read through Pkg."""
    def __init__(self, ui, win, binary):
        self.ui, self.m, self.win = ui, ui.m, win
        self.pkg = os88marty.Pkg(self.m, win, sym=ui.sym)
        self.state = npstate.State(self, str(binary))
        self.off = self.state.off
        self.state.check(0)

    # tools/notepad/state.py's Lab-compatible interface.
    def w(self, off):
        return self.pkg.word(off)

    def b(self, off):
        return self.pkg.byte(off)

    def word(self, name):
        return self.state.w(name)

    def byte(self, name):
        return self.state.b(name)

    def text(self):
        # The package AND its separately movable document claim are read
        # under one compactor-idle pause. A banked W_SEG/np_dseg is not safe.
        def read(m):
            rawlen, rawseg = self.pkg.reads([(self.off["np_len"], 2),
                                            (self.off["np_dseg"], 2)])
            n, seg = int.from_bytes(rawlen, "little"), int.from_bytes(rawseg, "little")
            return bytes(m.readseg(seg, 0, n)) if n else b""
        return os88marty.heap_read(self.m, read, "Note Pad document snapshot")

    def expect(self, expected):
        self.ui._wait(lambda: self.text() == expected,
                      "Note Pad bytes %r" % expected,
                      snapshot=lambda: self.text())
        self.state.check(len(expected))

    def front(self):
        self.ui.raise_window(self.win)
        os88marty.ui_done(self.m, "Note Pad to finish coming forward")

    def button(self, index):
        rawx, raww, rawy = self.pkg.reads([
            (self.off["np_pbx"] + index * 2, 2),
            (self.off["np_pbw"] + index * 2, 2),
            (self.off["np_pbtny"], 2)])
        x, w, y = (int.from_bytes(b, "little") for b in (rawx, raww, rawy))
        require(w, "Note Pad find button %d is not visible" % index)
        self.ui.mo.click(x + w // 2, y + 5, settle=0)
        os88marty.ui_done(self.m, "Note Pad find button to finish")

    def new(self):
        self.front()
        self.ui.menu_pick("File", "New")
        self.expect(b"")


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_xt_vga",
                    help="MartyPC VGA, CGA or Hercules machine name")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--npbin", default="build/notepad.bin")
    ap.add_argument("--notepad", default="B:/APPS/NOTEPAD.O88")
    ap.add_argument("--evidence", type=Path)
    ap.add_argument("--negative-release", action="store_true",
                    help="deliberately disable RELEASE; this run must fail")
    a = ap.parse_args(argv)
    image = Path(os88build.at(a.image)).resolve()
    apps = str(Path(os88build.at(a.apps)).resolve())
    binary = Path(os88build.at(a.npbin)).resolve()
    require(binary.is_file(), "missing raw Note Pad binary for state offsets")
    eq, syms = os88sym.equates(), os88sym.syms()
    build = Path(os88build.at("build")).resolve()
    build.mkdir(parents=True, exist_ok=True)
    if a.evidence:
        a.evidence.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="kbdlayouttext-", dir=build) as tmp:
        scratch = Path(tmp)
        fresh = scratch / "system.img"
        shutil.copyfile(image, fresh)
        set_file(fresh, "SYSTEM.CFG", None, scratch)
        with os88ui.boot(str(fresh), apps=apps, machine=a.machine, quick=True) as ui:
            m = ui.m
            page = KeyboardPage(ui, eq, syms)

            def evidence(name):
                if a.evidence:
                    ui.still()
                    w, h, px = m.fbuf()
                    path = a.evidence / (a.machine + "-" + name + ".png")
                    os88marty.write_png_rgb(str(path), w, h, px)
                    print("evidence:", path)

            def layout(item):
                page.open()
                page.pick(item, expected=item)
                evidence("keyboard-%d" % item)
                page.close()

            def launch():
                win = ui.path(a.notepad)
                os88marty.ui_done(m, "Note Pad launch to finish")
                return Note(ui, win, binary)

            def clients():
                if not page.word("mod_r_keyboard"):
                    return 0
                return int.from_bytes(page.keyboard("klm_clients", 2), "little")

            def count(n):
                ui._wait(lambda: clients().bit_count() == n,
                         "%d Note Pad glyph clients" % n,
                         snapshot=lambda: "clients=%04x" % clients())

            def glyph(note):
                note.front()
                ui.still()
                require(note.word("np_top") == 0, "first text row is scrolled away")
                x = note.word("np_tx") + 3 * 8       # enie in the first ES row
                y = note.word("np_ty")
                if page.byte("vid_kind") == 1:
                    # vram supplies guest coordinates on Hercules; the
                    # rendered MDA raster carries its adapter border offset.
                    _, _, rows = m.vram("herc")
                    tile = bytes(v for row in rows[y:y + 8] for v in row[x:x + 8])
                else:
                    tile = os88marty.crop_rgb(m, x, y, 8, 8)
                require(len(set(tile)) > 1, "accented glyph is a blank/solid tile")
                return tile

            first = launch()
            accumulated = b""
            for item, keys, expected in KEYS:
                layout(item)
                first.front()
                for key in keys:
                    ui.press(key)          # a dead key intentionally emits no word
                accumulated += expected
                first.expect(accumulated)
                print("physical layout", item, expected.hex())
            first.expect(EXPECTED)
            # A middle insertion and deletion must move CP850 bytes exactly.
            ui.press("Home").press("ArrowRight").press("ArrowRight").press("ArrowRight")
            ui.press("Digit2", need=True)
            first.expect(ES + LA + b"fr \x82a \x82 \x88 \x85")
            ui.press("Backspace", need=True)
            first.expect(EXPECTED)

            ui.menu_pick("Find", "Replace")
            ui._wait(lambda: first.byte("np_fpan") == 2 and
                     first.byte("np_ffield") == 0, "the Replace panel's Find field")
            ui.press("Digit2", need=True)
            require(first.pkg.read(first.off["np_fpat"], 2) == b"\x82\0",
                    "Find field did not accept CP850 e-acute")
            ui.press("Enter", need=True)
            at = EXPECTED.index(b"\x82")
            require((first.word("np_sel0"), first.word("np_sel1")) == (at, at + 1),
                    "Find did not select the extended byte")
            ui.press("Tab", need=True)
            require(first.byte("np_ffield") == 1, "Tab did not focus Replace")
            ui.press("BracketLeft").press("KeyE", need=True)
            require(first.pkg.read(first.off["np_frep"], 2) == b"\x88\0",
                    "Replace field did not accept composed e-circumflex")
            first.button(2)                 # live mapped Repl button
            first.expect(REPLACED)
            # Retained search strings are glyph clients too: clear both
            # through their real editors before testing New's final release.
            ui.press("Home").press("Delete")
            require(first.word("np_frepn") == 0, "Replace field did not clear")
            ui.press("Tab").press("Tab").press("Home").press("Delete")
            require(first.word("np_fpatn") == 0, "Find field did not clear")
            ui.press("Escape", need=True)
            first.expect(REPLACED)
            evidence("note-three-layouts")
            before = glyph(first)

            # One hold per instance, independently of which layout is active.
            second = launch()
            require(first.win.i != second.win.i, "second launch reused the first window")
            layout(2)
            second.front()
            ui.press("Semicolon", need=True)
            second.expect(b"\xa4")
            count(2)
            layout(0)
            require(page.word("mod_r_keyboard") and page.layout() == 0,
                    "BIOS default discarded a live document's font provider")
            first.front()
            ui.press("Home")                 # dismiss any document selection
            ui.menu_pick("Find", "Find")     # both transitions force full text redraw
            ui.press("Escape", need=True)
            first.expect(REPLACED)
            require(glyph(first) == before,
                    "BIOS-default repaint changed the document's accented glyph")
            evidence("note-bios-default")

            # Filename entry intentionally uses the selected BIOS layout;
            # the documents retain their extended bytes and separate holds.
            first.front()
            ui.menu_pick("File", "Save As")
            ui.chooser_save("KBTEXT.TXT")
            first.expect(REPLACED)
            second.front()
            ui.menu_pick("File", "Save As")
            ui.chooser_save("KBTWO.TXT")
            second.expect(b"\xa4")
            flush = os88flush.Flush(marty=m)
            require(bytes(flush.volume(1).read("MEDIA/KBTEXT.TXT")) ==
                    REPLACED.replace(b"\r", b"\r\n"), "saved CP850 text bytes differ")
            require(bytes(flush.volume(1).read("MEDIA/KBTWO.TXT")) == b"\xa4",
                    "second instance saved the wrong byte")
            count(2)
            print("save: independently decoded FAT bytes match both documents")

            if a.negative_release:
                def break_release(mm):
                    seg = page.word("mod_r_keyboard")
                    off, target = syms["klm_release"], syms["klm_status"]
                    require(bytes(mm.readseg(seg, off, 3)) == b"\x80\xfe\xff",
                            "RELEASE negative-control patch anchor moved")
                    rel = (target - off - 3) & 0xffff
                    mm.write((seg << 4) + off, b"\xe9" + rel.to_bytes(2, "little"))
                os88marty.heap_read(m, break_release, "disable keyboard RELEASE")
                print("negative control armed: RELEASE skips to status; expecting failure")

            second.new()
            count(1)
            require(page.word("mod_r_keyboard"), "New released the other instance's hold")
            first.expect(REPLACED)
            require(glyph(first) == before, "other instance's New damaged the glyph")
            second.front()
            ui.menu_pick("File", "Open")
            ui.chooser_open("KBTEXT.TXT")
            second.expect(REPLACED)
            count(2)
            first.front()
            ui.close(first.win)
            count(1)
            second.expect(REPLACED)
            require(glyph(second) == before, "closing the first instance damaged reopened text")
            evidence("note-reopened")
            second.new()
            ui._wait(lambda: not page.word("mod_r_keyboard"),
                     "the final New to release KEYBOARD.DRV")
            require(clients() == 0, "final glyph ownership remains")
            ui.close(second.win)
            print("PASS physical layouts, CP850 edit/find/replace/save/reopen and two-instance lifetime")


if __name__ == "__main__":
    main()
