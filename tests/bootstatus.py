#!/usr/bin/env python3
"""Does the boot say WHAT it is doing, and not only how far? (SPEC.md 15.6)

    make && python3 tests/bootstatus.py

SPEC.md 15.3's bar counts sectors, so from `kmain` onward it creeps through
`SPL_POST` notches while the machine mounts A:, reads `SYSTEM.CFG` and pulls
one `.DRV` off the floppy per wanted driver - seconds of drive noise on the
field machine with nothing on the screen naming any of it. 15.6 puts a line
under the bar. This is that line, checked two ways because neither is enough
on its own.

  1. **The COMPOSED line, read out of the overlay.** `spl_mline` lives in
     `.ovl`, which is part of stage 2's blob (SPEC.md 2.9.6) - so its segment
     is `[spl_fseg]`, whatever stage 2 chose, and reading it through that word
     is what makes the assertion exact: the three lines are compared as
     STRINGS rather than inferred from lit pixels.

  2. **...and the PIXELS, because a composed line nobody drew is the failure
     this feature is most likely to have.** The band under the bar is
     sampled once a displayed frame and hashed, so every distinct thing that
     appeared there is counted; four is the answer (three lines plus the
     desktop arriving), and a build that composed without drawing scores one.

**The last line cannot be caught by 1 and that is not a defect.** `kmain`
frees the overlay the instruction after `drv_boot` returns, and
`Starting os8088` is composed a few hundred instructions before that - so the
buffer holding it is released within the same displayed frame, while the
pixels stay until `wm_paint_all`. Assertion 2 is what covers it, which is
half of why there are two.

The disk is built here rather than taken from `build/`: the shipped image
carries no `SYSTEM.CFG` at all, so nothing is wanted and there is no driver
line to see (SPEC.md 51.3). Two rows are ticked - Ethernet and, on kern_big,
the RAM disk - because `n/N` with N = 1 cannot tell an ordinal from a total.
"""
import hashlib
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import os88marty                                            # noqa: E402
import os88sym                                              # noqa: E402

ROOT = os.path.normpath(os.path.join(HERE, ".."))
BUILD = os.path.join(ROOT, "build")

# SYSTEM.CFG's driver bitmap is by BIT and not by row (drv_cfgbit), which is
# what lets SPEC.md 31.1 reorder the page without re-ticking every settings
# floppy - so these two numbers are stable across a reorder and are the whole
# reason this test can name the drivers it expects.
BIT_ETHER = 1 << 4
BIT_RAM = 1 << 2

MACHINES = (
    # machine,                  the vram kind, and the row the line is on
    ("os8088_5150_cga_gla",  "cga",  None),
    ("os8088_5150_herc_gla", "herc", None),
)


def cfgfile(bits):
    """A SYSTEM.CFG carrying nothing but the driver bitmap.

    It must be NAMED system.cfg on disk: os88disk.py takes the 8.3 name from
    the source basename, and a `system2.cfg` lands as SYSTEM2.CFG - which the
    kernel does not look for, so the machine boots perfectly and wants no
    driver at all. That is a harness failure that reads exactly like the
    feature not working.
    """
    d = os.path.join(BUILD, "bootstatus")
    os.makedirs(d, exist_ok=True)
    p = os.path.join(d, "system.cfg")
    with open(p, "wb") as f:
        f.write(b"O88CFG\0\0" + (3).to_bytes(2, "little")
                + b"DW" + bytes([1, 2]) + bits.to_bytes(2, "little") + b"\0\0")
    return p


def image(bits):
    """build/ether360.img's own recipe, with our settings file in it."""
    cmd = subprocess.run(["make", "-n", "build/ether360.img"], cwd=ROOT,
                         capture_output=True, text=True, check=True).stdout
    cmd = cmd.replace("\\\n", " ").strip()
    assert "build/system.cfg" in cmd, cmd
    out = os.path.join(BUILD, "bootstatus", "boot.img")
    cmd = (cmd.replace("build/system.cfg", cfgfile(bits))
              .replace("-o build/ether360.img", "-o " + out))
    subprocess.run(cmd, cwd=ROOT, shell=True, check=True,
                   stdout=subprocess.DEVNULL)
    return out


def run(img, machine, kind, want):
    lin_live = os88sym.linear("spl_live")
    lin_seg = os88sym.linear("spl_fseg")
    lin_entry = os88sym.linear("cold_entry")
    sect = os88sym.sections()
    assert sect["spl_mline"] == ".ovl", sect["spl_mline"]
    off_line = os88sym.syms()["spl_mline"]   # blob-relative, `.ovl` having a
                                                # vstart of OVL_AT (SPEC.md
                                                # 2.9.6): the offset in the map
                                                # IS the offset in the segment

    lines, bands, last = [], set(), None
    with os88marty.launch(img, machine=machine, boot=0) as m:
        started = False
        for _ in range(4000):
            m.advance(frames=1)
            live = m.read(lin_live, 1)[0]
            if not started:
                # [spl_live] means nothing until the kernel's own bytes are on
                # the machine: during POST that address is RAM nothing has
                # written, and it reads as a live splash about one frame in.
                if live != 1 or m.read(lin_entry, 1)[0] != 0xE9:
                    continue
                started = True

            seg = int.from_bytes(m.read(lin_seg, 2), "little")
            if seg:
                txt = m.readseg(seg, off_line, 33).split(b"\0")[0]
                txt = txt.decode("latin1").rstrip()
                if txt.isprintable() and txt and (not lines or lines[-1] != txt):
                    lines.append(txt)

            w, h, rows = m.vram(kind)
            band = bytes(rows[h // 2 + 16 + 3])
            if sum(band):
                d = hashlib.md5(band).hexdigest()
                if d != last:
                    last, _ = d, bands.add(d)
            if live == 0:
                break
        else:
            raise SystemExit("%s: the splash never handed the screen over"
                             % machine)
        m.advance(frames=240)
        screen = os88marty._Screen(m)

    print("  %-22s lines %r" % (machine, lines))
    print("  %-22s %d distinct status bands, desktop: %s"
          % ("", len(bands), screen))

    fail = []
    if lines[:1] != ["Loading SYSTEM.CFG"]:
        fail.append("the settings read is not announced first: %r" % lines[:1])
    for i, title in enumerate(want, 1):
        exp = "Loading Driver %d/%d (%s)" % (i, len(want), title)
        if exp not in lines:
            fail.append("no %r in %r" % (exp, lines))
    # three lines plus the desktop arriving; a build that composed without
    # ever drawing scores one, and one that redrew per sector scores dozens
    if not 3 <= len(bands) <= 6:
        fail.append("%d distinct status bands, expected 3..6" % len(bands))
    if screen.field < 0.9 or screen.rule > 0.05 or screen.dock < 0.9:
        fail.append("the desktop did not come up: %s" % screen)
    return fail


def main():
    big = "KERN_SMALL" not in os.environ.get("OS88_DEFINES", "")
    want = ["Ethernet", "Ram Disk"] if big else ["Ethernet"]
    bits = BIT_ETHER | (BIT_RAM if big else 0)
    img = image(bits)

    bad = []
    for machine, kind, _ in MACHINES:
        bad += ["%s: %s" % (machine, f) for f in run(img, machine, kind, want)]
    if bad:
        for b in bad:
            print("FAIL", b)
        return 1
    print("bootstatus: SPEC.md 15.6's line is composed and drawn on both "
          "1bpp adapters")
    return 0


if __name__ == "__main__":
    sys.exit(main())
