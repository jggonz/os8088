#!/usr/bin/env python3
"""The COMPRESSED KERNEL boots, and every byte of it is right (SPEC.md 2.9.13).

    make && python3 tests/kzboot.py

`KERNEL.SYS` ships packed past the blob and the plain head, and stage 2
expands it with an UNBOUNDED LZ4 decoder that `mem_unblob` gives back to the
heap at the end of `kmain`. Two things follow, and this row is both of them:

  * IT REACHES A DESKTOP. A boot that does not is the only failure this change
    can have that anybody would notice by themselves.
  * ...AND THE IMAGE IN MEMORY IS THE IMAGE ON THE HOST, byte for byte, for
    all of it. That is the assertion that matters and the one a screenshot
    cannot make: a decoder that got one match wrong still boots, still draws a
    desktop, and is a kernel with a wrong instruction somewhere in it.

**THE COMPARISON IS AT THE HANDOFF AND NOT AT THE DESKTOP**, and that is not
fussiness. Stage 2 writes into the image before it jumps - `boot_cylrun` at
+4, the boot timer at +12, the loading screen's segment - and kmain writes a
great deal more, so a comparison taken at the desktop reports 103 differing
bytes on a kernel that expanded perfectly. A breakpoint on KERNEL_SEG:0 is the
one moment the image is exactly what the file says, and there the whole of it
can be compared rather than a window somebody had to justify.

**TWO GEOMETRIES, because there are two floppy boot sectors that reach this.**
The 360KB disk is the tight one and gets the byte comparison; the 1.44MB disk
is a DIFFERENT 512 bytes (`boot.bin`, 18 spt against 9) and gets a boot. The
720KB disk shares its sector with the 360KB one and differs only in a BPB
whose spt and heads are identical, so booting it would test nothing this does
not; the 1.2MB disk's sector is genuinely its own and no emulator here has a
5.25" HD drive, so `t_canary` and `t_image` are its cover and
`t_buildmatrix` keeps it assembling. The HARD DISK is a third loader again
(2.9.13.5) and `tests/hdboot.py` is what boots it.

`--nokzip` is the A/B: the same four images built with the knob that turns
this off, which is how "the packed disk boots" is told from "any disk boots".
**It is built in a PRIVATE TREE** (tools/os88build.py) rather than in
`build/`, so the arm neither leaves a knob kernel where every other row's
symbol map points nor has to spend a second full build putting the shared
directory back.
"""
import argparse
import json
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import os88build                                       # noqa: E402
import os88marty                                       # noqa: E402

ROOT = os.path.join(os.path.dirname(__file__), "..")
KERNEL_SEG = 0x0060


def say(*a):
    print(*a, flush=True)


def kzdefs(t):
    """What tools/os88kz.py published about the pack the Makefile just did."""
    return json.load(open(t.img("kernel.kz.json")))


def blob_bytes():
    """BOOT2_SECS * 512 - the blob, read out of the source the Makefile reads.

    kernel.bin is [ the blob ][ the image ] and only the second half lands at
    KERNEL_SEG; stage 1 reads the first into BLOB_SEG. Under KZIP os88kz.py
    publishes it, and on the other arm there is nothing to publish - so it
    comes from kernel.asm rather than from a 4096 typed here, which is the
    same constant one edit away from being wrong.
    """
    src = open(os.path.join(ROOT, "kernel", "kernel.asm")).read()
    m = re.search(r"^BOOT2_SECS\s+equ\s+(\d+)", src, re.M)
    if not m:
        sys.exit("kzboot: no BOOT2_SECS in kernel/kernel.asm")
    return int(m.group(1)) * 512


def spl_resident():
    """SPL_RESIDENT * 512 - the plain head, read out of the source.

    The Makefile's own $(KZ_HEAD), and the reason this row needs it on BOTH
    arms is that the region is not a decoder boundary: it is where stage 2
    WRITES before it jumps (18.93.1's boot_cylrun at +4, the timer at +12,
    spl_fseg), so an unpacked kernel differs there too and comparing over it
    would be asserting about those writes.
    """
    src = open(os.path.join(ROOT, "kernel", "splash.inc")).read()
    m = re.search(r"^SPL_RESIDENT\s+equ\s+(\d+)", src, re.M)
    if not m:
        sys.exit("kzboot: no SPL_RESIDENT in kernel/splash.inc")
    return int(m.group(1)) * 512


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--machine144", default="os8088_5150_herc_gla_144")
    ap.add_argument("--nokzip", action="store_true",
                    help="the A/B: build with the knob that turns this OFF")
    a = ap.parse_args()

    # ONE TREE, AND EVERYTHING IS READ OUT OF IT - the four images, the two
    # kernel files and os88kz's json. That is the whole of the old warning
    # here ("a knob build in between leaves kernel.bin, kernel.sys and the
    # images describing different things, and the row then reads one through
    # another's numbers"): a tree cannot be half a build, so the failure it
    # describes is not available any more.
    IMGS = ("os8088-360.img", "apps360.img", "os8088.img", "apps.img")
    t = (os88build.tree("NOKZIP=1", targets=IMGS) if a.nokzip
         else os88build.plain()).apply()

    fails = []
    packed = os.path.getsize(t.img("kernel.sys"))
    image = open(t.img("kernel.bin"), "rb").read()
    if a.nokzip:
        say("kzboot: NOKZIP=1 - KERNEL.SYS is the image, %d bytes" % packed)
        if packed != len(image):
            fails.append("NOKZIP=1 still produced a packed KERNEL.SYS (%d vs "
                         "the image's %d) - the knob did not reach the build"
                         % (packed, len(image)))
        blob, head = blob_bytes(), spl_resident()
    else:
        n = kzdefs(t)
        blob, head = n["blob"], n["head"]
        say("kzboot: KERNEL.SYS %d -> %d bytes, %d sectors instead of %d, "
            "%d block(s), R %d"
            % (n["image"], packed, n["ksecs"], n["ksecs_plain"], n["nblk"],
               n["r"]))
        if packed >= n["image"]:
            fails.append("KERNEL.SYS is not smaller than the kernel")

    # PAST THE BLOB: kernel.bin is [ the blob ][ the image ] and only the
    # second half lands at KERNEL_SEG - stage 1 reads the first into BLOB_SEG.
    want = image[blob:]
    with os88marty.launch(t.img("os8088-360.img"),
                          apps=t.img("apps360.img"),
                          machine=a.machine, boot=False) as m:
        m.bp_exec(KERNEL_SEG << 4)      # the handoff, and the only moment the
        m.run()                         # image is exactly what the file says
        if not m.wait_stop(limit=120):
            fails.append("stage 2 never reached KERNEL_SEG:0 - the boot did "
                         "not get as far as handing over")
            return report(fails)
        say("kzboot: 360KB: stage 2 handed over, so the whole image expanded")
        got = b""
        while len(got) < len(want):
            k = min(0x8000, len(want) - len(got))
            got += m.readseg(KERNEL_SEG + (len(got) >> 4), 0, k)
        # THE DECODER'S OWN TERRITORY IS PAST THE PLAIN HEAD, and only that is
        # asserted. Stage 2 writes into the head before it jumps - SPEC.md
        # 18.93.1's boot_cylrun at +4, the boot timer at +12, and the loading
        # screen's segment - so the head differs by ~42 bytes on a boot that
        # went perfectly, and asserting over it would be asserting about those
        # writes rather than about the decoder. They are reported, because a
        # change in how many there are is worth seeing.
        hbad = [i for i in range(head) if got[i] != want[i]]
        if head:
            say("kzboot: the plain head differs in %d byte(s)%s - stage 2's "
                "own writes before the jump (18.93.1's +4, the timer at +12)"
                % (len(hbad), (" at %s" % hbad[:6]) if hbad else ""))
        who = "THE READ'S" if a.nokzip else "THE DECODER'S"
        if got[head:] == want[head:]:
            say("kzboot: %s %d BYTES ARE THE FILE, byte for byte"
                % (who, len(want) - head))
        else:
            bad = [i for i in range(head, len(want)) if got[i] != want[i]]
            fails.append("%d of %s %d bytes differ, first at %d "
                         "(%d past the head, block %d)"
                         % (len(bad), who.lower(), len(want) - head, bad[0],
                            bad[0] - head, (bad[0] - head) // 0xF000))
        m.bp_exec()                     # ...and let it finish booting, so a
        m.run()                         # kernel that expanded right but cannot
        os88marty.settle(m, gate=os88marty.desktop_up)   # RUN is still a
        say("kzboot: 360KB: ...and it reaches a desktop")  # failure

    # ...AND THE OTHER FLOPPY BOOT SECTOR. build/boot.bin is 18 sectors a
    # track where boot360.bin is 9, so its run arithmetic - which is what the
    # packed tail's 512-aligned R was got wrong against once - is a different
    # set of immediates that nothing else here boots.
    with os88marty.launch(t.img("os8088.img"), apps=t.img("apps.img"),
                          machine=a.machine144) as m:
        os88marty.settle(m, gate=os88marty.desktop_up)
        say("kzboot: 1.44MB: a desktop, through the OTHER boot sector")

    return report(fails)


def report(fails):
    for f in fails:
        say("  FAIL: " + f)
    say("kzboot: %s" % ("FAILED" if fails else "ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
