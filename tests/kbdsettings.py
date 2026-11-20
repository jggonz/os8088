#!/usr/bin/env python3
"""Drive the optional Keyboard page and independently read its saved record.

All five choices are picked through the actual radio controls, and physical keys are
checked in the page's sample. Spanish needs its CP850 enie, French its e-acute.
Closing writes KL v1/length1; a new boot must restore that choice and an
unrelated clock preference. Missing, mismatched and invalid KL records must
leave BIOS default and preserve the clock preference. A disk with no
KEYBOARD.DRV must refuse an explicit pick without changing the current choice
or dirtying SYSTEM.CFG. There is a valid activation before that refusal arm.

This is a soak row. Run only after the integrated images have been built;
it never assembles a fixture or changes the caller's images. The lead measures
its runtime and runs the deliberate red arm when registering it.
"""
import argparse
import os
from pathlib import Path
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import os88build                                                # noqa: E402
import os88fat                                                  # noqa: E402
import os88flush                                                # noqa: E402
import os88geom                                                 # noqa: E402
import os88marty                                                # noqa: E402
import os88lz                                                   # noqa: E402
import os88sym                                                  # noqa: E402
import os88ui                                                   # noqa: E402


CHOICES = ("BIOS default", "US QWERTY", "Spanish (Spain)",
           "Latin American Spanish", "French AZERTY")
# Independent expected bytes, not the module's generated translation tables.
VECTORS = (("KeyQ", b"q"), ("KeyQ", b"q"),
           ("Semicolon", b"\xa4"), ("Semicolon", b"\xa4"),
           ("Digit2", b"\x82"))


def require(ok, what):
    if not ok:
        raise AssertionError(what)


def records(data):
    """Read the on-disk envelope without using the guest's parser."""
    require(data[:8] == b"O88CFG\0\0" and data[8:10] == b"\x03\0",
            "SYSTEM.CFG has the wrong signature or generation")
    out, at = {}, 10
    while at + 2 <= len(data):
        key = data[at:at + 2]
        if key == b"\0\0":
            return out
        require(at + 4 <= len(data), "truncated record header")
        n = data[at + 3]
        require(at + 4 + n <= len(data), "truncated record payload")
        require(key not in out, "duplicate record %r" % key)
        out[key] = (data[at + 2], data[at + 4:at + 4 + n])
        at += 4 + n
    raise AssertionError("SYSTEM.CFG has no terminator")


def replace_kl(data, replacement):
    """Keep every other saved record byte-for-byte for the fallback arms."""
    records(data)
    out, at = bytearray(data[:10]), 10
    while data[at:at + 2] != b"\0\0":
        end = at + 4 + data[at + 3]
        if data[at:at + 2] != b"KL":
            out += data[at:end]
        at = end
    return bytes(out) + replacement + b"\0\0"


def set_file(image, name, data, scratch):
    fat = os88fat.Fat12(str(image))
    fat.delete(name)
    if data is not None:
        staged = scratch / "staged.bin"
        staged.write_bytes(data)
        fat.add(str(staged), name)
    fat.save()


class KeyboardPage:
    def __init__(self, ui, eq, syms):
        self.ui, self.m, self.eq = ui, ui.m, eq
        self.off = dict(syms, **eq)

    def byte(self, name):
        return self.m.read(self.ui.sym(name), 1)[0]

    def word(self, name):
        return int.from_bytes(self.m.read(self.ui.sym(name), 2), "little")

    def module_bytes(self, seg, name, count):
        return bytes(self.m.readseg(seg, self.off[name], count)) if count else b""

    def keyboard(self, name, count=1):
        seg = self.word("mod_r_keyboard")
        require(seg, "KEYBOARD.DRV is not loaded while reading %s" % name)
        return self.module_bytes(seg, name, count)

    def layout(self):
        return self.keyboard("klm_layout")[0] if self.word("mod_r_keyboard") else 0

    def sample(self):
        fp = self.ui.sym("CPFP") + self.eq["CPE_PAINT"] * 4 + 2
        seg = int.from_bytes(self.m.read(fp, 2), "little")
        n = self.module_bytes(seg, "cp_key_len", 1)[0]
        return self.module_bytes(seg, "cp_key_text", n)

    def window(self):
        return self.ui.wait_window("Control Panel")

    def open(self):
        self.ui.menu_pick("Apple", "Control Panel")
        cp = self.ui.wait_window("Control Panel")
        hide = self.byte("cp_hide")
        record = self.eq["CP_IKEY"]
        require(not (hide & (1 << record)), "Keyboard page is hidden")
        row = sum(not (hide & (1 << r)) for r in range(record))
        # Content geometry comes from the build's own equates, anchored to
        # the live window; this row is above the list's first scroll stop.
        x = cp.x + 1 + self.eq["CP_IX"] + 20
        y = cp.y + os88geom.TITLE_H + 1 + self.eq["CP_I0Y"]
        y += row * self.eq["CP_IROWH"] + self.eq["CP_IROWH"] // 2
        self.ui.mo.click(x, y, settle=0)
        self.ui._wait(lambda: self.byte("cp_sel") == record,
                      "the Keyboard page to be selected", os88ui.T_NAV)
        os88marty.ui_done(self.m, "the Keyboard page to finish drawing")

    def pick(self, item, expected=None):
        cp = self.window()
        x = cp.x + 1 + self.eq["CP_RX"] + self.eq["CP_PLX"] + 20
        y = cp.y + os88geom.TITLE_H + 1 + self.eq["CPQ_R0Y"]
        y += item * self.eq["CPQ_ROWH"] + 6
        self.ui.mo.click(x, y, settle=0)
        if expected is not None:
            self.ui._wait(lambda: self.layout() == expected,
                          "layout %s to activate" % CHOICES[expected], os88ui.T_WINDOW)
        os88marty.ui_done(self.m, "the Keyboard transaction to finish")

    def expect_sample(self, expected):
        self.ui._wait(lambda: self.sample() == expected,
                      "sample bytes %r (got %r)" % (expected, self.sample()), os88ui.T_NAV)

    def bios_byte(self, value):
        """Fixture for a BIOS that produces CP850 while the module is absent.

        Translation is tested with physical keys above. This one word tests
        the clean-close glyph hold: an international BIOS can provide an
        accented byte without the user changing Control Panel preferences.
        """
        self.m.pause()
        try:
            ring = self.m.read(os88marty.BIOS_KBUF, 4)
            head = int.from_bytes(ring[:2], "little")
            tail = int.from_bytes(ring[2:], "little")
            require(head == tail and 0x1e <= tail < 0x3e,
                    "BIOS-ring fixture did not start empty")
            self.m.write(0x400 + tail, bytes([value, 0]))
            after = tail + 2 if tail < 0x3c else 0x1e
            self.m.write(os88marty.BIOS_KBUF + 2, after.to_bytes(2, "little"))
        finally:
            self.m.run()

    def close(self):
        self.ui.close(self.window())
        os88marty.ui_done(self.m, "the Control Panel save to finish")


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--small", action="store_true", help="128KB small-kernel session-only choices")
    ap.add_argument("--negative-persistence", action="store_true",
                    help="corrupt the saved choice; the reboot assertion must fail")
    a = ap.parse_args(argv)
    if a.small:
        os88build.use_build("build/smallk")
        os.environ["OS88_DEFINES"] = "KERN_SMALL"
        if a.image == "build/os8088-360.img":
            a.image = "build/small360.img"
        if a.machine == "os8088_5150_herc_gla":
            a.machine = "os8088_5150_cga_128k"
    image = Path(os88build.at(a.image)).resolve()
    apps = str(Path(os88build.at(a.apps)).resolve())
    eq, syms = os88sym.equates(), os88sym.syms()
    build = Path(os88build.at("build")).resolve()
    build.mkdir(parents=True, exist_ok=True)

    def boot(path):
        return os88ui.boot(str(path), apps=apps, machine=a.machine, quick=True)

    with tempfile.TemporaryDirectory(prefix="kbdsettings-", dir=build) as tmp:
        scratch = Path(tmp)
        fresh, saved = scratch / "fresh.img", scratch / "saved.img"
        shutil.copyfile(image, fresh)
        set_file(fresh, "SYSTEM.CFG", None, scratch)
        with boot(fresh) as ui:
            p = KeyboardPage(ui, eq, syms)
            require(p.layout() == 0 and not p.word("mod_r_keyboard"),
                    "a default boot loaded the optional keyboard module")
            p.open()
            require(not p.word("mod_r_keyboard"),
                    "painting/querying BIOS default loaded KEYBOARD.DRV")
            for item, (key, expected) in enumerate(VECTORS):
                p.pick(item, expected=item)
                ui.press(key, need=True)
                p.expect_sample(expected)
                ui.press(key, need=True).press("Backspace", need=True)
                p.expect_sample(expected)
                ui.press("Escape", need=True)
                p.expect_sample(b"")
                print("pick/sample:", CHOICES[item], expected.hex())
            # Extended sample bytes must remain readable after choosing BIOS
            # default; clearing the last such byte releases the private hold.
            p.pick(2, expected=2)
            ui.press("Semicolon", need=True)
            p.expect_sample(b"\xa4")
            p.pick(0, expected=0)
            require(p.word("mod_r_keyboard") and p.keyboard("klm_builtin") == b"\x01",
                    "BIOS default dropped the sample's glyph hold")
            ui.press("Escape", need=True)
            ui._wait(lambda: not p.word("mod_r_keyboard"),
                     "clearing the accented sample to release KEYBOARD.DRV", os88ui.T_NAV)
            p.pick(4, expected=4)
            ui.press("Digit2", need=True)
            p.expect_sample(b"\x82")
            if a.small:
                p.close()
                require(p.layout() == 4, "closing small panel lost the live layout")
                flush = os88flush.Flush(marty=ui.m)
                require(not flush.dirty(0), "small session-only choice wrote to disk")
                flush.save(0, str(saved))
            else:
                clock = p.byte("clk_h12") ^ 1
                ui.m.write(ui.sym("clk_h12"), bytes([clock]))
                require(p.byte("cp_wdirty"), "layout picks did not mark settings dirty")
                p.close()
                require(not p.byte("cp_dsave"), "closing did not write SYSTEM.CFG")
                require(p.keyboard("klm_builtin") == b"\0",
                        "closing left the Control Panel glyph hold alive")
                flush = os88flush.Flush(marty=ui.m)
                cfg = bytes(flush.volume(0).read("SYSTEM.CFG"))
                require(records(cfg).get(b"KL") == (1, b"\x04"),
                        "saved keyboard record is not KL v1/length1/French")
                flush.save(0, str(saved))

        if a.small:
            with boot(saved) as ui:
                p = KeyboardPage(ui, eq, syms)
                require(not p.word("mod_r_keyboard") and p.layout() == 0,
                        "small session-only choice persisted across restart")
                p.open()
                ui.press("KeyQ", need=True)
                p.expect_sample(b"q")
                p.close()
            print("PASS small128 all choices, sample holds and session-only restart")
            return

        if a.negative_persistence:
            set_file(saved, "SYSTEM.CFG", replace_kl(cfg, b"KL\x01\x01\x01"), scratch)
            print("negative control: saved French replaced with US; expecting reboot failure")

        with boot(saved) as ui:
            p = KeyboardPage(ui, eq, syms)
            require(p.layout() == 4, "saved French choice did not activate at boot")
            require(p.byte("clk_h12") == clock, "keyboard save lost the clock preference")
            p.open()
            ui.press("Digit2", need=True)
            p.expect_sample(b"\x82")
            p.close()
            print("close/save/reboot: KL v1, French and clock preference restored")
            p.open()
            p.pick(0, expected=0)
            p.close()               # save BIOS default and clear cp_wdirty
            p.open()
            require(not p.byte("cp_wdirty") and not p.word("mod_r_keyboard"),
                    "clean-close arm did not start with clean BIOS default")
            p.bios_byte(0xa4)
            p.expect_sample(b"\xa4")
            require(not p.byte("cp_wdirty") and p.word("mod_r_keyboard"),
                    "BIOS sample changed preferences or failed to hold glyphs")
            p.close()
            require(not p.word("mod_r_keyboard"),
                    "a clean panel close leaked the sample glyph hold")
            print("clean close: BIOS-only accented sample releases KEYBOARD.DRV")
            # The existing writer must refuse a foreign system volume while
            # preserving the already selected keyboard and retry flag.
            p.open(); p.pick(4, expected=4)
            restored, foreign = scratch / "before-save-failure.img", scratch / "foreign.img"
            os88flush.Flush(marty=ui.m).save(0, str(restored))
            shutil.copyfile(apps, foreign)
            ui.m.mount(0, str(foreign))
            tick = int.from_bytes(ui.m.read(0x46c, 4), "little")
            ui._wait(lambda: (int.from_bytes(ui.m.read(0x46c, 4), "little") - tick) &
                     0xffffffff >= 40, "the XT motor timeout before detecting swapped media", 5)
            p.close()
            ui.wait_toast(says="Not Saved: No Sys in A:")
            require(p.byte("cp_dsave") == 1 and p.byte("cp_wdirty") and p.layout() == 4,
                    "save failure lost the working layout or retry flag")
            require(not os88flush.Flush(marty=ui.m).dirty(0),
                    "a refused settings save wrote to the foreign disk")
            ui.m.mount(0, str(restored))
            print("save refusal: French remains active, retry retained, foreign disk untouched")

        for name, replacement in (("missing", b""),
                                  ("wrong-version", b"KL\x02\x01\x04"),
                                  ("wrong-length", b"KL\x01\x02\x04\0"),
                                  ("invalid-id", b"KL\x01\x01\xff")):
            altered = scratch / (name + ".img")
            shutil.copyfile(saved, altered)
            set_file(altered, "SYSTEM.CFG", replace_kl(cfg, replacement), scratch)
            with boot(altered) as ui:
                p = KeyboardPage(ui, eq, syms)
                require(p.layout() == 0 and not p.word("mod_r_keyboard"),
                        "%s KL record did not leave BIOS default" % name)
                require(p.byte("clk_h12") == clock,
                        "%s KL record lost the unrelated preference" % name)
            print("fallback:", name)

        mismatch = bytearray(os88lz.cz_unwrap(
            os88fat.Fat12(str(image)).read("KEYBOARD.DRV")))
        require(mismatch[:2] == b"O8", "keyboard refusal fixture has no module header")
        mismatch[4] ^= 0x40                  # valid image, wrong build number
        mismatched_file, _ = os88lz.cz_wrap(bytes(mismatch), os88lz.LZ4, filt=True)
        for label, data in (("absent", None), ("mismatched", mismatched_file)):
            refused = scratch / (label + ".img")
            shutil.copyfile(fresh, refused)
            set_file(refused, "KEYBOARD.DRV", data, scratch)
            with boot(refused) as ui:
                p = KeyboardPage(ui, eq, syms)
                p.open()
                require(not p.byte("cp_wdirty"), "refusal arm began dirty")
                p.pick(2)
                ui.wait_toast(says="Keyboard not loaded")
                require(p.layout() == 0 and not p.word("mod_r_keyboard"),
                        "refused module changed the working BIOS choice")
                require(not p.byte("cp_wdirty"), "refused activation dirtied preferences")
                ui.press("KeyQ", need=True)
                p.expect_sample(b"q")
                p.close()
                require(not os88flush.Flush(marty=ui.m).dirty(0),
                        "a refused choice wrote to the system disk")
                print("refusal:", label, "KEYBOARD.DRV keeps BIOS input and disk untouched")

    print("PASS Keyboard choices, sample, persistence, fallback and refusal")


if __name__ == "__main__":
    main()
