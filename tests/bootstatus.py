#!/usr/bin/env python3
"""Does the boot say WHAT it is doing, and not only how far? (SPEC.md 15.6)

    make && python3 tests/bootstatus.py

SPEC.md 15.3's bar counts sectors, so from `kmain` onward it creeps through
`SPL_POST` notches while the machine mounts A:, reads `SYSTEM.CFG` and pulls
one `.DRV` off the floppy per wanted driver - seconds of drive noise on the
field machine with nothing on the screen naming any of it. 15.6 puts a line
under the bar. This is that line, checked two ways because neither is enough
on its own.

**Five lines now, and the first two are 15.6.4's.** `Looking for Mouse` and
`Looking for Drives` name the two phases of `kmain` that are not disk at all
and are the longest waits on a hard-disk boot's bar - 9.4.1's identify window
and 18.97's TRACK 0 question. They are composed from `kmain` rather than from
`drv_boot`, so this row is what proves the ORDER: a line composed before the
notch that raises `[spl_live]` would be composed, never drawn, and invisible
to assertion 1's buffer read only if it were also overwritten - which it is.

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
import struct
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
    """build/ether360.img's own recipe, with our settings file in it.

    ONLY THE os88disk LINE, and that is the whole of what this docstring is
    for. The recipe is TWO commands - a `python3 -c` that writes
    build/system.cfg, then the disk build that puts it on a floppy - and this
    used to take both, substitute our path into the text, and run the pair.
    The substitution hit both occurrences, so the generator wrote ETHER360's
    OWN bitmap over the file we had just written, and the disk came out asking
    for one driver where we had asked for two.

    It failed as `no 'Loading Driver 1/2 (Ethernet)' in [... 'Loading Driver
    1/1 (Ethernet)']`, which reads as the KERNEL miscounting - and the kernel
    was right every time. Dropping the generator is the fix; the assertion
    below is what stops it coming back, because the whole failure was a disk
    that did not carry what this function was asked for.
    """
    recipe = subprocess.run(["make", "-n", "build/ether360.img"], cwd=ROOT,
                            capture_output=True, text=True, check=True).stdout
    recipe = recipe.replace("\\\n", " ")
    lines = [l for l in recipe.splitlines() if "os88disk.py" in l]
    assert len(lines) == 1, recipe
    cmd = lines[0].strip()
    assert "build/system.cfg" in cmd, cmd
    out = os.path.join(BUILD, "bootstatus", "boot.img")
    cmd = (cmd.replace("build/system.cfg", cfgfile(bits))
              .replace("-o build/ether360.img", "-o " + out))
    subprocess.run(cmd, cwd=ROOT, shell=True, check=True,
                   stdout=subprocess.DEVNULL)
    got = cfg_on_disk(out)
    assert got == bits, ("the disk asks for driver bitmap 0x%02X and this test "
                         "asked for 0x%02X - the recipe substitution has put "
                         "somebody else's settings file on it" % (got, bits))
    return out


def cfg_on_disk(img):
    """The driver bitmap in the SYSTEM.CFG that is ACTUALLY on `img`.

    Read back rather than trusted: this test's whole failure mode was building
    a disk that disagreed with what it had asked for, and no assertion further
    down could tell that from the kernel getting the count wrong.
    """
    d = open(img, "rb").read()
    bps = struct.unpack("<H", d[11:13])[0]
    spc = d[13]
    res = struct.unpack("<H", d[14:16])[0]
    nfat = d[16]
    rootn = struct.unpack("<H", d[17:19])[0]
    fatsz = struct.unpack("<H", d[22:24])[0]
    rootlba = res + nfat * fatsz
    datalba = rootlba + (rootn * 32 + bps - 1) // bps
    for i in range(rootn):
        e = d[rootlba * bps + i * 32:rootlba * bps + i * 32 + 32]
        if e[0] == 0:
            break
        if e[0:11] == b"SYSTEM  CFG":
            clus = struct.unpack("<H", e[26:28])[0]
            off = (datalba + (clus - 2) * spc) * bps
            # O88CFG\0\0, the key count, 'DW', the key's id and width, then
            # the value - cfgfile() above writes exactly this shape
            return struct.unpack("<H", d[off + 14:off + 16])[0]
    raise AssertionError("no SYSTEM.CFG on %s" % img)


def run(img, machine, kind, want):
    lin_live = os88sym.linear("spl_live")
    lin_seg = os88sym.linear("spl_fseg")
    lin_entry = os88sym.linear("cold_entry")
    sect = os88sym.sections()
    assert sect["spl_mline"] == ".ovl", sect["spl_mline"]
    off_msg = os88sym.syms()["spl_l_msg"]    # ...and the ROW it is drawn on,
                                             # which splash.inc banks for every
                                             # adapter (SPEC.md 15.3.5.1). It
                                             # was `h // 2 + 16 + 3` here, a
                                             # second copy of a formula that
                                             # moved - and the row it then
                                             # sampled was the PROGRESS BAR,
                                             # which changes every notch
    off_line = os88sym.syms()["spl_mline"]   # blob-relative, `.ovl` having a
                                                # vstart of OVL_AT (SPEC.md
                                                # 2.9.6): the offset in the map
                                                # IS the offset in the segment

    lines, bands, last, prev = [], set(), None, None
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
                if txt != prev:
                    prev = txt          # ...and only once it has SETTLED: the
                    txt = None          # composer walks spl_mline a cell at a
                                        # time, so a sample landing mid-walk
                                        # reads a HYBRID of the old line and
                                        # the new - 'Looking for Druse' is
                                        # 'Dr' of Drives over 'use' of Mouse,
                                        # and it is not a prefix, so the
                                        # collapse below cannot see it. The
                                        # glass never shows it: 15.6.3 draws
                                        # the composed line in ONE font_run
                if txt and txt.isprintable() and (not lines or lines[-1] != txt):
                    # spl_msg_cs walks spl_mline a cell at a time (15.6.1), so
                    # a sample landing mid-compose reads a strict PREFIX of the
                    # line that arrives a frame later. That is the composer
                    # working, not a line, and it is only visible at all
                    # because the buffer IS the record of the glass.
                    if lines and txt.startswith(lines[-1]):
                        lines[-1] = txt
                    else:
                        lines.append(txt)

            w, h, rows = m.vram(kind)
            msg = int.from_bytes(m.readseg(seg, off_msg, 2), "little") if seg else 0
            band = bytes(rows[msg + 3]) if 0 < msg < h - 3 else b""
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
    head = ["Looking for Mouse", "Looking for Drives", "Loading SYSTEM.CFG"]
    if lines[:3] != head:
        fail.append("15.6.4's two lines do not precede the settings read: %r"
                    % lines[:3])
    for i, title in enumerate(want, 1):
        exp = "Loading Driver %d/%d (%s)" % (i, len(want), title)
        if exp not in lines:
            fail.append("no %r in %r" % (exp, lines))
    # FIVE lines plus the desktop arriving (15.6.4); a build that composed
    # without ever drawing scores one, and one that redrew per sector scores
    # dozens. The floor is 4 rather than 6 because the sampler is once a
    # displayed frame and the last line can land inside the frame the desktop
    # does - which is assertion 2's own caveat, one line longer now.
    if not 4 <= len(bands) <= 9:
        fail.append("%d distinct status bands, expected 4..9" % len(bands))
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
