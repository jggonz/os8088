#!/usr/bin/env python3
"""EXTD.DRV's lifetime: loaded for Extend, dropped for Single, and a refusal
that leaves the machine Single (SPEC.md 39.19.6).

    make && python3 tests/extdmod.py

The extended desktop's placement policy is an on-demand module, and what makes
that safe is ONE invariant: `[vid_ndisp]` > 1 only while the image is mounted.
Every kernel caller far-calls the module's slot with no thunk, so a slot left
naming a freed claim after a drop, or a desktop left extended with no image,
is a far call into memory somebody else now owns - and it does not fault,
because the claim is still readable. The dual-display rows (`disp*`) all drive
Extend and would notice a load that never happened; none of them asks whether
the image went AWAY again, or what a machine without the file does. This does,
on four private fixture disks built from the shipped boot sector, kernel and
modules - dockmodule.py's shape:

  A. load / drop / reload through the real Control Panel: Extend claims the
     image and makes two displays, Single collapses and FREES it with every
     slot back on `mod_gone`, and Extend again claims it again.
  B. the same click with EXTD.DRV not on the disk: REFUSED - [vid_dmode] back
     to Single, one display, nothing claimed, and the panel's own toast.
  C. a boot with Extend saved in SYSTEM.CFG loads the image before the first
     paint and comes up on two displays.
  D. the same SYSTEM.CFG with no EXTD.DRV: boots, Single, nothing claimed.

After every step the invariant itself is asserted, not just the step's own
expectation: two displays with no image is a FAIL wherever it is seen.
"""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path[:0] = [str(HERE), str(HERE.parent / "tools")]
import os88build                                            # noqa: E402
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
import dispcp                                               # noqa: E402

S = os88sym.linear
MACHINE = "os8088_5150_both_gla"        # CGA + Hercules: the pairing
                                        # vid_dual_ok accepts (SPEC.md 39.19)
ROOT = HERE.parent


def b(m, name):
    return m.read(S(name), 1)[0]


def row(m, eq):
    """mod_tab's MOD_EXT row: dw seg, dw file. The segment is the claim."""
    return int.from_bytes(m.read(S("mod_tab") + eq["MOD_EXT"] * 4, 2),
                          "little")


def state(m, eq):
    return row(m, eq), b(m, "vid_ndisp"), b(m, "vid_dmode")


FAILS = []


def invariant(m, eq, where):
    r, n, d = state(m, eq)
    if n > 1 and r == 0:
        FAILS.append("%s: [vid_ndisp] = %d with EXTD.DRV NOT mounted - SPEC.md "
                     "39.19.6's invariant is broken, and every EXTCALL lands "
                     "on mod_gone while the desktop is extended" % (where, n))
    return r, n, d


def slots_at_rest(m, eq, where):
    """Every EXT slot back on mod_gone - dockmodule.py's check, one module
    along. A drop that left one naming the image would far-call freed memory
    the next time a window is put down, and it would not fault: the claim is
    still readable."""
    off = os88sym.syms()["mod_gone"]
    want = off.to_bytes(2, "little") + eq["COLD_SEG"].to_bytes(2, "little")
    base = S("EXFP")
    got = m.read(base, eq["EXT_NENT"] * 4)
    if got != want * eq["EXT_NENT"]:
        FAILS.append("%s: an EXTD.DRV slot still points into module memory: "
                     "%s" % (where, got.hex()))


def fixture(folder, name, extd, extend_saved):
    files = [os88build.at("build/ctrl.drv")]
    if extd:
        files.append(os88build.at("build/extd.drv"))
    if extend_saved:
        # 'VM' ver 2, three bytes (driver.inc's CFG_DATA): the adapter, then
        # SPEC.md 39.19's [vid_dmode] = Extend and [vid_dlay] = Right. The
        # adapter byte is 0xFF, which vid_avail_test's range check refuses, so
        # the boot takes drv_boot_x's .vnosw arm and applies the ARRANGEMENT on
        # whichever card the probe chose - this test is about the two bytes,
        # not about a switch.
        d = folder / name
        d.mkdir()
        cfg = d / "system.cfg"          # the basename IS the 8.3 name
        cfg.write_bytes(b"O88CFG\0\0" + (3).to_bytes(2, "little")
                        + b"VM" + bytes([2, 3, 0xFF, 1, 0]) + b"\0\0")
        files.append(str(cfg))
    image = folder / (name + ".img")
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", str(image),
                    "--size", "360", "--boot", os88build.at("build/boot360.bin"),
                    "--kernel", os88build.at("build/kernel.sys"), *files],
                   check=True, stdout=subprocess.DEVNULL, cwd=str(ROOT))
    return str(image)


def click_mode(m, mo, which):
    """dispcp.set_mode's click without its wait, which asserts the mode TOOK -
    arm B's whole point is that it must not."""
    i = dispcp.MODES[which]
    wx, wy = dispcp._cp_win(m, S)
    mo.click(wx + 1 + dispcp.CP_RX + dispcp.CP_PGX + i * dispcp.CPV_MSTEP + 20,
             wy + dispcp.TITLE_H + 1 + dispcp.CPV_MY + 6, settle=0)


def boot(image):
    m = os88marty.launch(image, machine=MACHINE)
    os88marty.no_saver(m)
    return m


def arm_a(image, eq, fails):
    with boot(image) as m:
        mo = os88mouse.Mouse(marty=m)
        r, n, d = invariant(m, eq, "A boot")
        print("  %-15s row=%04x ndisp=%d dmode=%d" % ("A boot:", r, n, d))
        if r or n != 1:
            fails.append("A: a Single boot claimed EXTD.DRV or came up "
                         "extended (row %04x, ndisp %d)" % (r, n))
        dispcp.open_panel(m, mo, S, os88marty.settle)
        seen = []
        for step, want in (("right", True), ("single", False),
                           ("below", True)):
            dispcp.set_mode(m, mo, S, os88marty.settle, step)
            r, n, d = invariant(m, eq, "A " + step)
            print("  %-15s row=%04x ndisp=%d dmode=%d" % ("A " + step + ":", r, n, d))
            if want and not (r and n == 2 and d == 1):
                fails.append("A %s: expected the image claimed and two "
                             "displays, got row %04x ndisp %d dmode %d"
                             % (step, r, n, d))
            if not want:
                if r or n != 1 or d:
                    fails.append("A %s: expected the image DROPPED and one "
                                 "display, got row %04x ndisp %d dmode %d"
                                 % (step, r, n, d))
                slots_at_rest(m, eq, "A " + step)
            seen.append(r)
        dispcp.close_panel(m, mo, S, os88marty.settle)
        r, n, d = invariant(m, eq, "A panel closed")
        if not (r and n == 2):
            fails.append("A: closing the panel dropped the image under an "
                         "extended desktop (row %04x ndisp %d)" % (r, n))


def arm_b(image, eq, fails):
    with boot(image) as m:
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_panel(m, mo, S, os88marty.settle)
        before = m.read(S("toast_buf"), 24).split(b"\0")[0]
        click_mode(m, mo, "right")
        try:
            os88marty.until(
                m, lambda _: m.read(S("toast_buf"), 24).split(b"\0")[0]
                != before, "the refusal's toast", poll=0.05, guest=20.0)
        except Exception:
            pass
        os88marty.settle(m)
        toast = m.read(S("toast_buf"), 24).split(b"\0")[0]
        r, n, d = invariant(m, eq, "B refused")
        print("  %-15s row=%04x ndisp=%d dmode=%d toast=%r"
              % ("B refused:", r, n, d, toast.decode("latin-1")))
        if r or n != 1 or d:
            fails.append("B: with no EXTD.DRV the Extend click should leave "
                         "Single, one display and nothing claimed - got row "
                         "%04x ndisp %d dmode %d" % (r, n, d))
        if not toast.startswith(b"Needs Sys Disk"):
            fails.append("B: the refusal said nothing (toast %r) - SPEC.md "
                         "2.8.4: the caller owes the reason" % toast)
        slots_at_rest(m, eq, "B refused")


def arm_boot(image, eq, fails, arm, want_ext):
    with boot(image) as m:
        r, n, d = invariant(m, eq, arm + " boot")
        print("  %-15s row=%04x ndisp=%d dmode=%d" % (arm + " boot:", r, n, d))
        if want_ext and not (r and n == 2 and d == 1):
            fails.append("%s: Extend saved in SYSTEM.CFG, EXTD.DRV on the disk "
                         "- expected an extended boot, got row %04x ndisp %d "
                         "dmode %d" % (arm, r, n, d))
        if not want_ext:
            if r or n != 1 or d:
                fails.append("%s: Extend saved and no EXTD.DRV - expected a "
                             "Single boot with nothing claimed, got row %04x "
                             "ndisp %d dmode %d" % (arm, r, n, d))
            slots_at_rest(m, eq, arm + " boot")


def main():
    eq = os88sym.equates()
    if "MOD_EXT" not in eq:
        sys.exit("extdmod: this kernel has no MOD_EXT - kern_small? "
                 "EXTD.DRV is kern_big's alone (SPEC.md 39.19.6)")
    fails = FAILS
    with tempfile.TemporaryDirectory(prefix="os88-extd-") as tmp:
        t = Path(tmp)
        print("A: load / drop / reload through the panel")
        arm_a(fixture(t, "a", True, False), eq, fails)
        print("B: the panel's Extend with no EXTD.DRV")
        arm_b(fixture(t, "b", False, False), eq, fails)
        print("C: a boot with Extend saved")
        arm_boot(fixture(t, "c", True, True), eq, fails, "C", True)
        print("D: a boot with Extend saved and no EXTD.DRV")
        arm_boot(fixture(t, "d", False, True), eq, fails, "D", False)
    print()
    for f in fails:
        print("extdmod: FAIL: " + f)
    if fails:
        return 1
    print("extdmod: EXTD.DRV loads for Extend, drops for Single, and a "
          "missing file is Single at the panel and at boot - PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
