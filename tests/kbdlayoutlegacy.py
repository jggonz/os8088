#!/usr/bin/env python3
"""Actual DOS/WIN16 owners retain BIOS keyboard input with French active.

A private system image boots KL=French. The shipped DOS entry and current
WIN16 entry must publish IF_RAWKBD on the actual instance owning their
window. Physical Q/A/W/Z must enqueue q/a/w/z, not French a/q/z/w. DOS is
also checked inside its real fullscreen UI, using dosaltenter's helpers;
focusing native Control Panel afterward must restore French translation.

WIN16 is launched idle: no external/reference NE program is required. Its
header and I_NAME are synthetically renamed to exercise focus/name changes
on that real owner. This does not claim a Windows program was executed or
that its program-driven entry rename was exercised. Module CPU coverage
separately proves the bypass is driven by IF_RAWKBD independently of I_NAME.

Runner wants=: system image and build/kbdlayoutlegacy.img, a360KB image
containing the existing build/win16.o88 as WIN16.O88. No new guest program,
build invocation or shared image mutation happens in this row.
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
import os88marty                                                # noqa: E402
import os88sym                                                  # noqa: E402
import os88ui                                                   # noqa: E402
import dosaltenter                                              # noqa: E402
from kbdsettings import KeyboardPage, require, set_file           # noqa: E402

RAW = 8
KEYS = ("KeyQ", "KeyA", "KeyW", "KeyZ")
BIOS = b"qawz"
FRENCH = b"aqzw"


def u16(data, off=0):
    return int.from_bytes(data[off:off + 2], "little")


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_xt_vga")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--fixture", default="build/kbdlayoutlegacy.img")
    a = ap.parse_args(argv)
    require(not os.environ.get("OS88_MEGA"),
            "kbdlayoutlegacy targets the real-mode DOS/WIN16 entries, not kern_mega")
    image = Path(os88build.at(a.image)).resolve()
    fixture = Path(os88build.at(a.fixture)).resolve()
    require(fixture.is_file(), "build the declared WIN16-only fixture before this row")
    eq, syms = os88sym.equates(), os88sym.syms()
    build = Path(os88build.at("build")).resolve()
    build.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="kbdlegacy-", dir=build) as tmp:
        scratch = Path(tmp)
        system = scratch / "system.img"
        shutil.copyfile(image, system)
        cfg = b"O88CFG\0\0\x03\0KL\x01\x01\x04\0\0"
        set_file(system, "SYSTEM.CFG", cfg, scratch)
        with os88ui.boot(str(system), apps=str(fixture), machine=a.machine,
                         quick=True) as ui:
            m, S = ui.m, ui.sym
            page = KeyboardPage(ui, eq, syms)
            require(page.layout() == 4, "the private image did not boot French active")

            def owner(win):
                pkg = os88marty.Pkg(m, win, sym=S)

                def read(mm):
                    slot = mm.read(S("wm_owner") + win.i, 1)[0]
                    require(slot < eq["INST_MAX"], "legacy window has no real owner")
                    at = S("inst_tab") + slot * eq["I_RECSZ"]
                    rec = bytes(mm.read(at, eq["I_RECSZ"]))
                    require(rec[eq["I_STATE"]] == 1 and
                            rec[eq["I_KIND"]] & eq["KIND_PKG"] and
                            u16(rec, eq["I_SPTR"]) == pkg.seg,
                            "window does not identify its published package instance")
                    require(rec[eq["I_FLAGS"]] & RAW,
                            "actual entry lost IF_RAWKBD before/at publication")
                    return slot, at, rec
                return os88marty.heap_read(m, read, "legacy owner/RAW snapshot")

            def words(expected, caption, idle=True):
                ui._wait(lambda: not m.read(0x417, 1)[0] & 0x0f,
                         "%s to release BIOS Shift/Ctrl/Alt" % caption,
                         os88ui.T_SHORT)
                got = []
                for key in KEYS:
                    tail = u16(m.read(os88marty.BIOS_KBUF + 2, 2))
                    require(0x1e <= tail < 0x3e and not tail & 1,
                            "BIOS ring tail is outside its conventional word slots")
                    m.key(key)
                    ui._wait(lambda: u16(m.read(os88marty.BIOS_KBUF + 2, 2)) != tail,
                             "%s to enqueue %s" % (caption, key), os88ui.T_SHORT)
                    after = u16(m.read(os88marty.BIOS_KBUF + 2, 2))
                    require(after == (tail + 2 if tail < 0x3c else 0x1e),
                            "one physical letter did not enqueue exactly one BIOS word")
                    got.append(u16(m.read(0x400 + tail, 2)))
                    if idle:
                        os88marty.ui_done(m, "%s to consume %s" % (caption, key))
                require(bytes(w & 255 for w in got) == expected,
                        "%s words %s, expected low bytes %r" %
                        (caption, ["%04x" % w for w in got], expected))
                print(caption, "BIOS words", " ".join("%04x" % w for w in got))

            def native():
                require(page.layout() == 4, "legacy focus changed the selected French layout")
                page.open()
                page.expect_sample(b"")
                words(FRENCH, "native focus")
                page.expect_sample(FRENCH)
                page.close()

            # Positive native control ensures a globally untranslated
            # keyboard cannot pass the compatibility arms accidentally.
            native()
            dos = ui.path("A:/APPS/DOS.O88")
            ui.raise_window(dos)
            os88marty.ui_done(m, "the current DOS entry to publish")
            dos_slot, _, _ = owner(dos)
            words(BIOS, "DOS window")
            for _ in KEYS:
                ui.press("Backspace", need=True)  # undo the idle console's typed probe
            require(dosaltenter._bracket(ui) == dosaltenter.FSX_NONE,
                    "DOS already owns a fullscreen bracket before Alt+Enter")
            m.alt("Enter")
            require(dosaltenter._settle_bracket(ui, 0) == 0,
                    "actual DOS did not enter its fullscreen UI")
            require(owner(dos)[0] == dos_slot, "DOS fullscreen changed its RAW owner")
            words(BIOS, "DOS fullscreen", idle=False)
            for _ in KEYS:
                os88marty.key_taken(m, "Backspace", ui=False)
            m.alt("Enter", hold=0.08)
            require(dosaltenter._settle_bracket(ui, dosaltenter.FSX_NONE) ==
                    dosaltenter.FSX_NONE, "DOS did not return to its native window")
            os88marty.ui_done(m, "DOS fullscreen restore to complete")
            owner(dos)
            native()
            ui.close(dos)

            win16 = ui.path("B:/WIN16.O88")
            ui.raise_window(win16)
            os88marty.ui_done(m, "the current WIN16 entry to publish")
            slot, at, rec = owner(win16)
            words(BIOS, "WIN16 idle entry")
            pkg = os88marty.Pkg(m, win16, sym=S)
            header = bytes(pkg.read(16, 16))
            oldname = rec[eq["I_NAME"]:eq["I_NAME"] + 16]
            renamed = b"RENAMEDCLIENT\0".ljust(16, b"\0")

            def rename(mm):
                # A synthetic name change, explicitly not a reference NE
                # program run. Never locate an owner by this mutable name.
                pkg.write(16, renamed)
                mm.write(at + eq["I_NAME"], renamed)
            os88marty.heap_read(m, rename, "synthetic WIN16 owner rename")
            require(owner(win16)[0] == slot, "renaming changed WIN16's RAW owner")
            native()
            ui.raise_window(win16)
            os88marty.ui_done(m, "renamed WIN16 to regain focus")
            require(owner(win16)[0] == slot, "focus changed the renamed WIN16 owner")
            words(BIOS, "renamed WIN16 focus")

            def restore(mm):
                pkg.write(16, header)
                mm.write(at + eq["I_NAME"], oldname)
            os88marty.heap_read(m, restore, "restore WIN16 owner name")
            ui.close(win16)
            native()
            print("PASS actual DOS/WIN16 RAW owners, BIOS input and restored native French")
            print("gap: WIN16 was idle; no reference NE program execution or real program rename")


if __name__ == "__main__":
    main()
