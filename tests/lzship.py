#!/usr/bin/env python3
"""THE WHOLE SHIPPED SET, COMPRESSED, boots and runs (SPEC.md 20.13, 20.14).

    python3 tests/lzship.py --fmt lz4      # it builds its own tree

Every other row in this family compresses one subject: lzload three packages,
lzdrv one driver, lzmod one module. This is the configuration a user would
actually be handed - `make zset ZFMT=...` - where every shipped package, every
shipped driver and every data file on both 360KB floppies is compressed at
once, and the kernel that reads them is built to carry that format.

It exists because that configuration has a failure mode none of the single-
subject rows can have: the system disk's drivers are expanded during BOOT, by
a kernel that has not finished starting, into a heap that is still being laid
out. Nine of them, one after another.

FOUR ASSERTIONS:

  1. it BOOTS to a desktop, which is the drivers expanding;
  2. the drivers actually ATTACHED - the count off drv_tab, not off the
     screen, because a driver that failed to expand is a driver that is
     silently absent rather than one that says so;
  3. a compressed package on the apps disk OPENS - the loader's in-place
     expansion, on a package nobody chose for being easy;
  4. BEVERLY.MOD opens from MEDIA/ on the APPS disk. That last is the point
     of the whole exercise: at 360KB the module needs a floppy of its own
     (SPEC.md 24.4), and compressed it does not.

**IT BUILDS A PRIVATE TREE** (tools/os88build.py), which is what `make zset`
was working around. That target exists because the compressed images land at
the SAME paths a plain build uses, so it copied them aside and deleted the
originals either side - three steps to stop the next `make` shipping a
compressed floppy. With `BUILD=<tree>` there is nothing to copy and nothing to
delete: the set is built where nothing else looks, the shared `build/` is
never written, and the bare `make` this row used to run on its way out to put
the tree back is gone with it.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import os88build                                       # noqa: E402
import os88marty                                       # noqa: E402
import os88mouse                                       # noqa: E402
import os88sym                                         # noqa: E402
import dispcp                                          # noqa: E402
import os88geom                                        # noqa: E402
from trackmove import pkg_syms, find_win               # noqa: E402



def say(*a):
    print(*a, flush=True)


def compressed(img):
    """(compressed, total) shipped files on `img`, read HOST-side.

    A file is compressed if it carries the directory hint (data, SPEC.md
    20.14.1) or if its own header has flags bit 3 (a package, 20.13) or its
    image exceeds its file (a driver, 20.13.3.1). Three signals because there
    are three formats, and reading the FILE rather than trusting the entry is
    the same discipline the kernel uses.
    """
    d = open(img, "rb").read()
    bps = struct.unpack_from("<H", d, 11)[0]
    res = struct.unpack_from("<H", d, 14)[0]
    nfat, nent = d[16], struct.unpack_from("<H", d, 17)[0]
    fsz = struct.unpack_from("<H", d, 22)[0]
    spc = d[13]
    root = (res + nfat * fsz) * bps
    data = root + nent * 32
    seen = [0, 0]

    def first_sector(clus):
        return d[data + (clus - 2) * spc * bps:][:512] if clus >= 2 else b""

    def scan(off, n):
        for i in range(n):
            e = d[off + i * 32:off + i * 32 + 32]
            if e[0] in (0, 0xE5) or e[11] & 0x08:
                continue
            clus = struct.unpack_from("<H", e, 26)[0]
            if e[11] & 0x10:
                if e[0:1] != b"." and clus >= 2:
                    scan(data + (clus - 2) * spc * bps, spc * bps // 32)
                continue
            size = struct.unpack_from("<I", e, 28)[0]
            name = e[:11].decode("ascii", "replace")
            seen[1] += 1
            hdr = first_sector(clus)
            if e[12] in (0x5A, 0x5B):               # a 'CZ' data file, either
                                                    # format (SPEC.md 20.14.1)
                seen[0] += 1
            elif len(hdr) >= 12 and hdr[:2] == b"O8":
                if hdr[2] == 3 and hdr[3] & 0x08:   # a package
                    seen[0] += 1
                elif hdr[2] == 4 and struct.unpack_from("<H", hdr, 8)[0] > size:
                    seen[0] += 1                    # a driver
            del name
    scan(root, nent)
    return tuple(seen)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fmt", default="lz4", choices=("lz4", "lzb"))
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    a = ap.parse_args()

    # THE TWO KNOBS PAIRED, which is what `zset` was for: PKGZ compresses the
    # files and COMPRESS decides which decoder the kernel carries, and a disk
    # built with one and not the other is a floppy of programs that will not
    # open - a mismatch that reads as a broken loader. Here they are the tree's
    # key, so the two formats are two directories and neither is `build/`.
    #
    # `.apply()` and not just `.env`: this row resolves symbols in its own
    # frame (S below) AND through library helpers that take no defines -
    # os88marty.no_saver resolves ss_idle, launch's own gate resolves more -
    # and those go to os88sym's module default. Setting only the environment
    # fails exactly where the row is not looking.
    # ...AND media360.img WITH THEM: leg 4 boots it (SPEC.md 24.4 puts
    # BEVERLY.MOD on a floppy of its own at this geometry), and it is not
    # in DEFAULT_TARGETS, so without naming it the tree has no such file.
    t = os88build.tree("PKGZ=" + a.fmt, "COMPRESS=" + a.fmt,
                       targets=os88build.DEFAULT_TARGETS
                               + ("media360.img",)).apply()
    sysimg, appsimg = t.img("os8088-360.img"), t.img("apps360.img")
    S = os88sym.linear

    fails = []
    for img, tag in ((sysimg, "system"), (appsimg, "apps")):
        z, n = compressed(img)
        say("  %-8s %s: %d of %d files compressed, %d bytes"
            % (tag, os.path.basename(img), z, n, os.path.getsize(img)))
        if z < 2:
            fails.append("%s carries %d compressed files - the set was built "
                         "without PKGZ" % (tag, z))
    if fails:
        for f in fails:
            say("  FAIL: " + f)
        say("lzship: FAILED")
        return 1

    P = pkg_syms("apps/tracker/tracker.asm")
    plain = open("apps/tracker/beverly.mod", "rb").read()

    with os88marty.launch(sysimg, apps=appsimg, machine=a.machine) as m:
        # 1. it boots - which IS the drivers expanding, nine of them, during a
        #    boot that has not finished laying out the heap
        os88marty.settle(m, gate=os88marty.desktop_up)
        say("  boot       ok  (a desktop, so every driver expanded)")

        # 2. ...and they ATTACHED. drv_tab and not the screen: a driver that
        #    failed to expand is silently absent, not visibly broken.
        #
        # THE STRIDE AND THE ROW COUNT ARE ASKED FOR, not typed. This read
        # was `DRVR_SZ = 10` over sixteen rows where the record is
        # DRVR_SIZE = 16 and drv_tab has DRV_MAX of them - so it walked 256
        # bytes of a 80-byte table on a stride that matched no field, and
        # the number it printed was arithmetic on whatever follows. It only
        # ever asserted `live < 1`, which is why it never said so. Both are
        # `%ifdef`-dependent (kern_small has no RAM disk, so DRV_MAX is 4
        # there), which is the other reason not to type them.
        eq = os88sym.equates()
        sz, nrow = eq["DRVR_SIZE"], eq["DRV_MAX"]
        raw = m.read(S("drv_tab"), sz * nrow)
        live = sum(1 for i in range(nrow)
                   if raw[i * sz] | (raw[i * sz + 1] << 8))
        say("  drivers    %d attached" % live)
        if live < 1:
            fails.append("no driver attached: drv_tab is empty")

        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        wins = dispcp.win_list(m, S)
        if not wins:
            sys.exit("lzship: no Disk window after double-clicking B:")
        wx, wy = dispcp.win_rect(m, S, wins[-1])[:2]
        rows = [r[0] for r in dispcp.listing(m, S)]
        say("  B: lists   %r" % rows)

        # 3. a compressed PACKAGE opens, out of APPS/
        n0 = len(wins)
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "APPS")
        os88marty.settle(m)
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "CALC.O88")
        try:
            os88marty.until(m, lambda mm: len(dispcp.win_list(mm, S)) > n0,
                            "CALC.O88's window", poll=0.2, guest=30.0)
        except os88marty.MartyError:
            pass
        if len(dispcp.win_list(m, S)) > n0:
            say("  package    ok  (CALC.O88 opened, compressed)")
        else:
            fails.append("CALC.O88 did not open: the loader refused a "
                         "compressed package off the shipped disk")

    # 4. ...and the MODULE, off a floppy of ITS OWN.
    #
    #    It used to be MEDIA/ on the apps disk and that stopped being true:
    #    apps360 is at 346 of 354 clusters and BEVERLY.MOD is 42 of them
    #    lz4-packed, so it does not fit and no trimming makes it - taking
    #    AUDIO, MODPLUG and FONTVIEW off buys 27. SPEC.md 24.4 is exactly
    #    about that: at 360KB the module rides build/media360.img and the
    #    user swaps disks.
    #
    #    A SCRATCH DISK and not media360.img itself, because a swap is what
    #    24.4 asks of the USER and this harness has no verb for changing a
    #    floppy under a running guest. Booting media360 alone puts the
    #    module in B: with no TRACKER.O88 anywhere - the association then
    #    has no handler and the double-click opens the 'Open' chooser,
    #    which is the machine behaving correctly and the leg testing
    #    nothing. The pair on one image is the two halves of the swap, and
    #    what this leg is actually about is unchanged: 116KB of compressed
    #    MODULE expanding off a floppy into a claim.
    #
    #    Both inputs come out of the row's own tree, so they carry the
    #    format under test - scratch_disk resolves them and the output.
    #    t.img() AND NOT `build/...`: this row builds a PRIVATE tree, and
    #    os88build.apply() sets $OS88_BUILD alone - a private tree is not
    #    the RUN'S tree, so os88build.at() does not redirect for it and a
    #    `build/` path here reads the SHARED build. The lz4 arm passed
    #    that way by luck (build/zdata-lz4 exists) and the lzb arm, whose
    #    data only ever lives in the tree, failed loudly. The image is
    #    per-format for the same reason: two arms, two disks.
    modimg = os88marty.scratch_disk(
        t.img("lzship-mod360-" + a.fmt + ".img"),
        "APPS:" + t.img("tracker.o88"),
        "MEDIA:" + t.img("zdata-" + a.fmt + "/BEVERLY.MOD"), size=360)
    with os88marty.launch(sysimg, apps=modimg, machine=a.machine) as m:
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        os88marty.no_saver(m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        wins2 = dispcp.win_list(m, S)
        wx, wy = dispcp.win_rect(m, S, wins2[-1])[:2]
        # MEDIA/ on this disk too: $(MEDIAARGS360) prefixes it, so the
        # module sits in the same folder it does on the roomier geometries
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "MEDIA")
        os88marty.settle(m)
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "BEVERLY.MOD")
        try:
            os88marty.until(m, lambda mm: find_win(mm, S, "Tracker")[0],
                            "Tracker's window", poll=0.2, guest=60.0)
            # ...AND THEN THE CLAIM, which is what the twenty-second sleep
            # here was standing in for. [trk_modseg] going non-zero IS "the
            # 116KB expanded and Tracker holds it", so this costs what the
            # decode costs on this box and waits longer on a loaded one.
            def claimed(mm):
                seg = find_win(mm, S, "Tracker")[0]
                return seg and int.from_bytes(
                    mm.readseg(seg, P["trk_modseg"], 2), "little")

            os88marty.until(m, claimed, "Tracker to claim the module",
                            poll=0.2, guest=90.0)
        except os88marty.MartyError:
            pass
        os88marty.settle(m)
        # BY TITLE, and not by slot: CALC is open above, the Disk window has
        # been raised again, and wm_wins' last slot is whichever index the
        # window manager reused - not the newest window. Reading trk_modseg
        # out of CALC's segment answers a plausible number and then 116KB of
        # somebody else's memory, which is a failure that looks like a broken
        # decoder.
        pseg, _ = find_win(m, S, "Tracker")
        if not pseg:
            fails.append("no Tracker window: BEVERLY.MOD did not open from "
                         "the media disk (SPEC.md 24.4), which is the whole "
                         "reason this set exists")
        else:
            seg = int.from_bytes(m.readseg(pseg, P["trk_modseg"], 2), "little")
            got = b""
            while seg and len(got) < len(plain):
                k = min(0x8000, len(plain) - len(got))
                got += m.readseg(seg + (len(got) >> 4), 0, k)
            if got == plain:
                say("  module     ok  (all %d bytes, out of MEDIA/ on the "
                    "APPS disk)" % len(plain))
            elif not seg:
                # A SHOT, because [trk_modseg] = 0 means Tracker's own error
                # path ran and the reason is on the glass in words - which no
                # amount of reading memory recovers.
                shot = t.img("lzship-%s-fail.png" % a.fmt)
                m.shot(shot, rendered=True)
                fails.append("Tracker opened and holds no module - its own "
                             "error is in %s" % shot)
            else:
                bad = [i for i in range(len(plain)) if got[i] != plain[i]]
                fails.append("%d of %d module bytes differ, first at %d - "
                             "which is %s the 64KB boundary"
                             % (len(bad), len(plain), bad[0],
                                "past" if bad[0] >= 0x10000 else "before"))

    for f in fails:
        say("  FAIL: " + f)
    say("lzship: %s" % ("FAILED" if fails else "ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
