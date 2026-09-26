#!/usr/bin/env python3
"""Optional Dock module refusal and saved-setting fallback (SPEC.md 30.5).

Build private FAT12 fixtures from the shipped boot/kernel/modules. A missing
file and an invalid module must leave a working basic Dock and disarmed
callbacks; a saved advanced preference must not prevent booting without it.
The ordinary load/unload and drawing paths are exercised by dockpos.py.
"""
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path[:0] = [str(HERE), str(HERE.parent / "tools")]
import os88build
import os88marty
import os88mouse
import os88sym
import dispcp
import dockpos

S = os88sym.linear


def word(m, name):
    return int.from_bytes(m.read(S(name), 2), "little")


def check_basic(m, cfg):
    assert m.read(S("dock_cfg"), 1) == bytes([cfg]), "preference changed"
    assert word(m, "mod_r_dock") == 0, "failed load retained a module"
    assert word(m, "dock_thk") == 24, "fallback does not reserve a whole strip"
    assert m.read(S("dock_up"), 1) == b"\0", "fallback still open"
    assert word(m, "gfx_hole") == 0, "fallback retained a clipping hole"
    assert word(m, "vid_band_x0") == 0
    assert word(m, "vid_band_xe") == word(m, "vid_pw")
    assert word(m, "vid_dock_y0") == word(m, "vid_ph") - 24
    eq = os88sym.equates()
    off = os88sym.syms()["mod_gone"]
    expected = off.to_bytes(2, "little") + eq["COLD_SEG"].to_bytes(2, "little")
    base = S("DKFP")
    assert m.read(base, eq["DK_NENT"] * 4) == expected * eq["DK_NENT"], \
        "an unloaded callback still points into module memory"
    # ...and the same question about the OPERATIONS (SPEC.md 30.5): every
    # slot of dkv must rest on this kernel's own body. A fallback that left
    # one naming the image would far-jump into freed memory, which is the one
    # failure the mechanism has to be unable to produce - and it would not
    # fault, because the claim is still readable.
    dkv = m.read(S("dkv"), eq["DKI_N"] * 4)
    ks = eq["KERNEL_SEG"].to_bytes(2, "little")
    assert all(dkv[i + 2:i + 4] == ks for i in range(0, len(dkv), 4)), \
        "an unloaded Dock operation still points into module memory"


def fixture(folder, fault, saved):
    files = [os88build.at("build/ctrl.drv")]
    if fault == "corrupt":
        mod = bytearray(Path(os88build.at("build/dock.drv")).read_bytes())
        mod[0] ^= 0xff             # refuses both raw and CZ module images
        path = folder / "dock.drv"
        path.write_bytes(mod)
        files.append(str(path))
    if saved:
        cfg = folder / "system.cfg"
        cfg.write_bytes(b"O88CFG\0\0\x03\0DK\x01\x01\x06\0\0")
        files.append(str(cfg))
    image = folder / "system.img"
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", str(image),
                    "--size", "360", "--boot", os88build.at("build/boot360.bin"),
                    "--kernel", os88build.at("build/kernel.sys"), *files],
                   check=True, stdout=subprocess.DEVNULL)
    return str(image)


def run(fault, saved):
    with tempfile.TemporaryDirectory(prefix="os88-dock-") as tmp:
        image = fixture(Path(tmp), fault, saved)
        with os88marty.launch(image, machine="os8088_5150_herc_gla") as m:
            os88marty.no_saver(m)
            check_basic(m, 0)
            assert word(m, "mod_tab") == 0, "boot loaded Control Panel"
            mo = os88mouse.Mouse(marty=m)
            dispcp.open_panel(m, mo, S, os88marty.settle, page=None)
            wx, wy = dispcp._cp_win(m, S)
            row = m.read(S("cp_nst"), 1)[0] - 1
            mo.click(wx + 37, wy + 19 + 6 + row * 14 + 7, settle=0)
            os88marty.settle(m)
            for _ in range(2):
                dockpos.click_row(m, mo, dockpos.CPK_R0Y + dockpos.CPK_ROWH)
                check_basic(m, 0)
            # The panel remains alive and the desktop remains drawable.
            dockpos.full_repaint(m)
            assert dispcp._cp_win(m, S) is not None
            print("dockmodule: %s, saved=%s: refusal/fallback intact" % (fault, saved))


def main():
    for fault, saved in (("missing", False), ("corrupt", False), ("missing", True)):
        run(fault, saved)
    return 0


if __name__ == "__main__":
    sys.exit(main())
