#!/usr/bin/env python3
"""Confirmed shutdown, cancellation, and the live terminal splash (§12.3.2).

    make && python3 tests/shutdown.py
    make small && python3 tests/shutdown.py --small

The kernel tick must STOP while the BIOS clock and spinner keep advancing.
A screenshot alone cannot distinguish a shutdown from a frozen desktop.
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
SMALL = "--small" in sys.argv
if SMALL:
    os.environ["OS88_DEFINES"] = "KERN_SMALL"
    os.environ["OS88_BUILD"] = os.path.join(ROOT, "build", "smallk")
import os88marty as M
import os88sym as S
import os88ui


def word(m, name):
    return int.from_bytes(m.read(S.linear(name), 2), "little")


def run(card, machine, image):
    offsets, eq = S.syms(), S.equates()
    slot = S.linear("CPFP") + eq["CPE_SHUTDOWN"] * 4
    with os88ui.boot(image, apps="build/apps360.img", machine=machine,
                     settle=False) as ui:
        m = ui.m

        def seg():
            return int.from_bytes(m.read(slot + 2, 2), "little")

        def active():
            return (word(m, "mod_tab") != 0 and
                    m.readseg(seg(), offsets["sd_active"], 1) == b"\x01")

        def prompt():
            ui.menu_pick(0, "Shut Down...")
            M.until(m, lambda _: active(), "shutdown confirmation", limit=60)

        def cancelled(retained=False):
            # The prompt closes before a confirmed shutdown flushes settings.
            # Wait for the returning thunk to finish releasing its module too.
            M.until(m, lambda _: not active() and
                    bool(word(m, "mod_tab")) == retained,
                    "shutdown cancellation and module lifetime", limit=30)
            before = word(m, "ticks")
            M.guest_sleep(m, .5)
            assert word(m, "ticks") != before, "Cancel stopped the scheduler"
            assert bool(word(m, "mod_tab")) == retained, "Incorrect CTRL.DRV lifetime"

        prompt()
        w, h, rgb = m.fbuf()
        M.write_png_rgb("build/shutdown-%s-confirm.png" % card, w, h, rgb)
        m.key("Escape")
        cancelled()

        prompt()
        left = int.from_bytes(m.readseg(seg(), offsets["sd_left"], 2), "little")
        top = int.from_bytes(m.readseg(seg(), offsets["sd_top"], 2), "little")
        ui.mo.click(left + 224, top + 72)
        cancelled()

        prompt()
        # Press on Shut Down and slide out: release must leave the modal up.
        ui.mo.drag(left + 96, top + 72, left + 16, top + 48)
        assert active(), "Sliding out confirmed shutdown"
        m.key("Escape")
        cancelled()

        # An existing panel keeps its module and its pending settings on Cancel.
        ui.menu_pick(0, "Control Panel")
        panel = ui.wait_window("Control Panel")
        m.write(S.linear("cp_wdirty"), b"\x01")
        prompt()
        m.key("Escape")
        cancelled(retained=True)
        assert m.read(S.linear("cp_wdirty"), 1) == b"\x01"
        ui.close(panel)
        M.until(m, lambda _: word(m, "mod_tab") == 0, "panel module release", limit=30)

        if not SMALL:
            # Only the big kernel persists SYSTEM.CFG (SPEC.md 51.5.3). The
            # small writer is a one-byte RET beside the UI tail, not a file writer.
            # Inject a write-protected settings writer inside this private guest.
            # Shutdown must return to the live desktop with the dirty flag intact.
            m.write(S.linear("cp_wdirty"), b"\x01")
            prompt()
            error = eq["FERR_WPROT"]
            m.write(seg() * 16 + offsets["cp_cfg_save"],
                    bytes((0xB8, error & 255, error >> 8, 0xF9, 0xC3)))
            m.key("Enter")
            cancelled()
            assert m.read(S.linear("cp_wdirty"), 1) == b"\x01", "Failed save was forgotten"
            assert m.read(S.linear("cp_dsave"), 1) != b"\x00", "Failed save was not reported"

        # Pending settings must be written before stopping the OS.
        m.write(S.linear("cp_wdirty"), b"\x01")
        prompt()
        if card == "vga":
            m.key("Enter")
        else:
            ui.mo.click(left + 96, top + 72)
        old08 = m.read(S.linear("sch_old08"), 4)
        M.until(m, lambda _: m.read(0x20, 4) == old08,
                "BIOS timer handover", limit=60)
        M.guest_sleep(m, 1)
        frozen = word(m, "ticks")
        assert m.read(S.linear("cp_wdirty"), 1) == b"\x00", "Settings not flushed"
        assert m.inb(0x21) == 0xFE, "Non-timer IRQs remain enabled"
        assert m.inb(0x3F2) & 0xF0 == 0, "Floppy motor remains on"
        bios = int.from_bytes(m.read(0x46C, 2), "little")
        io = m.disk()
        angles, pictures = set(), set()
        for _ in range(20):
            M.guest_sleep(m, .25)
            angles.add(m.readseg(seg(), offsets["sd_cos"], 2))
            if card != "vga":
                pictures.add(bytes(b for row in m.vram(card)[2] for b in row))
        assert len(angles) >= 6, "Shutdown spinner froze"
        if card != "vga":
            assert len(pictures) >= 6, "Spinner state moved but pixels froze"
        assert word(m, "ticks") == frozen, "OS tasks still run after shutdown"
        after = m.disk()
        for key in ("reads", "writes", "read_sectors", "write_sectors"):
            assert after.get(key) == io.get(key), "Disk I/O continued after shutdown"
        assert int.from_bytes(m.read(0x46C, 2), "little") != bios, "BIOS clock froze"
        # Keys cannot restart or leave the terminal screen.
        m.key("Enter")
        m.key("Escape")
        M.guest_sleep(m, .5)
        assert word(m, "ticks") == frozen, "Keyboard restarted the OS"
        w, h, rgb = m.fbuf()
        M.write_png_rgb("build/shutdown-%s-terminal.png" % card, w, h, rgb)
        midx, midy = word(m, "vid_cw") // 2, word(m, "vid_ch") // 2
        if card == "vga":
            pixels = [[any(rgb[(y*w+x)*3:(y*w+x+1)*3]) for x in range(w)]
                      for y in range(h)]
        else:
            # Mono VRAM avoids the rendered Hercules aperture's extra rows.
            w, h, pixels = m.vram(card)
        for y, row in enumerate(pixels):
            for x, lit in enumerate(row):
                logo = midx - 60 <= x < midx + 60 and midy - 42 <= y < midy - 1
                text = midx - 140 <= x < midx + 140 and midy + 16 <= y < midy + 24
                assert not lit or logo or text, \
                    "Stale desktop/cursor pixels at %d,%d" % (x, y)
        print("shutdown: %s cancellation, settings flush, halted tasks and live spinner OK" % card)


def main():
    if SMALL:
        run("cga", "os8088_5150_cga_128k", "build/small360.img")
    else:
        for card, machine in (("vga", "os8088_xt_vga"),
                              ("cga", "os8088_5150_cga_gla"),
                              ("herc", "os8088_5150_herc_gla")):
            run(card, machine, "build/os8088-360.img")


if __name__ == "__main__":
    main()
