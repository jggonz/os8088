#!/usr/bin/env python3
"""Does a setting still survive the panel and a reboot? (SPEC.md 51.5.3)

    make && python3 tests/cfgtrip.py

The settings parser used to be one block of `.cold` with one copy of its
tables and one file buffer, and the reader and the writer were near calls to
each other. SPEC.md 51.5.3 split it in half: the READER is in the boot overlay
(SPEC.md 2.9.6) and the WRITER is inside CTRL.DRV, each with its own copy of
`_keys`, `_map`, `_bit`, `_sig` and `_buf`, emitted from one macro and read
`cs:` in both. Nothing in the build can tell whether those two copies still
agree about the file between them - the halves no longer share a segment, a
table or a buffer, and every way of getting that wrong assembles cleanly:

  * a `cs:` missed on one side reads the KERNEL at the image's offset, which
    on the write side means a plausible file full of the wrong bytes;
  * `ES` left naming the kernel means `dskw_write_sys` writes 120 bytes of
    .bss to the disk and `dskw_read` reads the file into it;
  * a `movsb` left in reads DS:SI, which SPEC.md 2.8.6 refuses in a module
    image and which nothing refuses in an overlay.

Every one of those still BOOTS, and most of them still draw a desktop. So the
assertion has to be the round trip itself: change a setting, close the panel
(which is the only thing that writes SYSTEM.CFG, SPEC.md 31.8), reboot the
disk that was written, and read the setting back out of the live kernel.

**The change is a POKE and not a click, deliberately.** What is under test is
the parser, not the Control Panel's coordinates - `cpc_pack` walks `_map` and
copies the live kernel bytes, which is exactly what a click leaves behind, and
`tests/dispcp.py` already drives the widgets. Poking the live bytes and
`[cp_wdirty]` puts the panel in the state a click would have, with no layout
constant in this file to go stale.

Run it against a kernel built before 51.5.3 and it passes; break either `cs:`
and it fails on the reboot, which is the A/B that says it contains the case.
"""
import argparse
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
import os88flush                                            # noqa: E402
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
import dispcp                                               # noqa: E402

S = os88sym.linear

# Three rows of drv_cfg_map, spread across it so a `cs:` missed on one cannot
# hide behind the row next to it: the clock's 12/24 (SPEC.md 31.5) is the
# first byte after the driver bitmap, and the screen saver's two (SPEC.md
# 79.4) are the last - and [ss_mins]'s unpack derives [ss_idle] through a far
# call OUT of the image, which is the one row that leaves the segment.
#
# **NOT [clk_secs]**, and the reason is the harness rather than the kernel: a
# menu bar showing seconds redraws once a second, so `settle` never sees a
# still screen and every leg below times out at 120s naming the boot. It is
# as good a row as any of these and there is no way to watch it from here.
WANT = {"clk_h12": 1, "ss_modes": 2, "ss_mins": 7}


def snap(m):
    return {n: m.read(S(n), 1)[0] for n in WANT}


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    a = ap.parse_args(argv)

    fail = []
    settle = os88marty.settle
    written = os.path.abspath(os.path.join("build", "cfgtrip-written.img"))

    with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
        before = snap(m)
        print("boot 1, defaults:", before)
        if before == WANT:
            sys.exit("cfgtrip: the disk already carries the values this "
                     "writes, so a pass would prove nothing")

        f = os88flush.Flush(marty=m)
        if f.dirty(0):
            fail.append("the system disk was already written to at boot")

        mo = os88mouse.Mouse(marty=m)
        dispcp.open_panel(m, mo, S, settle, page=None)
        for n, v in WANT.items():
            m.write(S(n), bytes([v]))
        m.write(S("cp_wdirty"), b"\x01")        # what a click would have left
        dispcp.close_panel(m, mo, S, settle)    # ...and this is what writes

        saved = m.read(S("cp_dsave"), 1)[0]
        print("boot 1, after the close: cp_dsave = %d (0 = written)" % saved)
        if saved:
            fail.append("cp_cfg_save refused: cp_dsave = %d" % saved)
        live = snap(m)
        if live != WANT:
            fail.append("the poke did not stick before the write: %s" % live)

        # WHAT REACHED THE DISK, read by a FAT12 walker that shares no code
        # with the kernel - so "os8088 wrote a file" and "there is a file" stop
        # being the same claim. MartyPC never writes a mounted floppy back on
        # its own (docs/MARTYPC-DEBUG.md), so without this the reboot below
        # boots the pristine image and passes nothing while looking like a
        # kernel bug.
        d = f.diff(0)
        print("boot 1, the disk: %d sector(s) changed, added %s"
              % (len(d["sectors"]), d["added"]))
        cfg = f.volume(0).read("SYSTEM.CFG")
        print("boot 1, SYSTEM.CFG: %d bytes, head %r" % (len(cfg), cfg[:8]))
        if cfg[:6] != b"O88CFG":
            fail.append("SYSTEM.CFG does not start with the signature: %r"
                        % cfg[:8])
        f.save(0, written)

    with os88marty.launch(written, apps=a.apps, machine=a.machine) as m:
        after = snap(m)
        print("boot 2, read back:", after)
        for n, v in WANT.items():
            if after[n] != v:
                fail.append("%s came back %d, wrote %d" % (n, after[n], v))
        seg = int.from_bytes(m.read(S("spl_fseg"), 2), "little")
        print("boot 2, spl_fseg = %04X (stage 2's blob, and the overlay in it, "
              "is dropped by here)" % seg)

    for f in fail:
        print("FAIL: %s" % f)
    print("cfgtrip: %s" % ("FAILED" if fail else "ok - the file round-trips "
                           "between two images that share no memory"))
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
