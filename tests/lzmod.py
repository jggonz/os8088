#!/usr/bin/env python3
"""BEVERLY.MOD, COMPRESSED, opened by a double-click (SPEC.md 20.14.5).

    make lzmodtest && python3 tests/lzmod.py

This is the file the whole feature is for. 116,085 bytes is 114 of a 360KB
disk's 354 clusters, which is why that geometry ships the module on a floppy of
its own (SPEC.md 24.4). LZ4 takes it to 42,177 - 36.3%, 42 clusters - so
Tracker and the module fit one disk with 294 clusters left, and ~145 sectors
of floppy time go away for ~1.2 seconds of decode.

It is also the only file in the tree that exercises the decoder's SEGMENT
CROSSING: 116KB is not a segment, so every path in SPEC.md 20.14.5 - the
bumped ES, the borrowed match source one segment down, lz_cross splitting a
copy at the boundary, and LZ_F_BUMP retiring the offset compare - runs here and
nowhere else.

FOUR ASSERTIONS, and the third is the one that makes the others worth having:

  1. the disk file really is compressed - the 'CZ' header and the directory
     hint, checked on the HOST before the machine is started, so a fixture
     that quietly stopped compressing cannot pass this row;
  2. a double-click on the .MOD row opens Tracker through the association
     (SPEC.md 54), which is the user-visible requirement in one action;
  3. all 116,085 bytes in the guest's claim are BYTE FOR BYTE the original.
     Nothing less will do: a decoder that got one match wrong across the
     64KB boundary still opens a window, still shows the title, and still
     plays - it plays a click;
  4. ...and Tracker holds it - `mp_loaded` - so the bytes above are a module
     the application accepted and not a buffer it read and rejected. Whether
     the mixer TICKS is a question about the machine's sound card and not
     about this feature, so `mp_row` is reported and not asserted, exactly as
     tests/trackmove.py does it.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import os88marty                                       # noqa: E402
import os88mouse                                       # noqa: E402
import os88sym                                         # noqa: E402
import os88build                                       # noqa: E402
import os88lz                                          # noqa: E402
import dispcp                                          # noqa: E402
from os88fixture import need                           # noqa: E402
from trackmove import pkg_syms                         # noqa: E402

SRC = "apps/tracker/beverly.mod"
PACKED = "build/lzf/BEVERLY.MOD"
IMG = "build/lzmod360.img"
CZ_MARK, CZ_M, CZ_H, CZ_L = 0x5A, 12, 13, 20   # +fmt: 20.14.2.4
S = os88sym.linear


def say(*a):
    print(*a, flush=True)


def report(fails):
    for f in fails:
        say("  FAIL: " + f)
    say("lzmod: %s" % ("FAILED" if fails else "ok"))
    return 1 if fails else 0


def dirents(img):
    """Every root-directory entry of a FAT12 image, raw."""
    d = open(img, "rb").read()
    bps = struct.unpack_from("<H", d, 11)[0]
    res = struct.unpack_from("<H", d, 14)[0]
    nfat, nent = d[16], struct.unpack_from("<H", d, 17)[0]
    fsz = struct.unpack_from("<H", d, 22)[0]
    off = (res + nfat * fsz) * bps
    for i in range(nent):
        e = d[off + i * 32:off + i * 32 + 32]
        if e[0] not in (0, 0xE5):
            yield e


def host_checks(fails):
    """Assertion 1, before a machine is involved.

    THROUGH `os88build.at`, because these two are the very files the guest is
    about to boot (14.2). Under a frozen run the tree holds them and `build/`
    may not, so reading the literal path either misses or - worse - asserts
    about one build's fixture and boots another's.
    """
    plain = open(SRC, "rb").read()
    blob = open(os88build.at(PACKED), "rb").read()
    parsed = os88lz.cz_parse(blob)
    if parsed is None:
        fails.append("%s is not a 'CZ' file" % PACKED)
        return plain
    fmt, n = parsed
    if n != len(plain) or os88lz.cz_unwrap(blob) != plain:
        fails.append("%s does not expand to %s" % (PACKED, SRC))
    say("  packed     %d -> %d bytes (%.1f%%, %s)"
        % (len(plain), len(blob), 100.0 * len(blob) / len(plain),
           os88lz.NAMES[fmt]))
    for e in dirents(os88build.at(IMG)):
        if e[:11] != b"BEVERLY MOD":
            continue
        hint = struct.unpack_from("<H", e, CZ_L)[0] | (e[CZ_H] << 16)
        size = struct.unpack_from("<I", e, 28)[0]
        ok = (CZ_MARK <= e[CZ_M] <= CZ_MARK + 1     # ...the mark CARRIES the
              and hint == len(plain)                # format (SPEC.md
              and size == len(blob))                # 20.14.2.4)
        say("  hint       mark=%02X unpacked=%d size=%d  %s"
            % (e[CZ_M], hint, size, "ok" if ok else "WRONG"))
        if not ok:
            fails.append("the directory hint on the disk is wrong")
        break
    else:
        fails.append("BEVERLY.MOD is not in %s" % IMG)
    return plain


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--fmt", default="lz4", choices=("lz4", "lzb"),
                    help="lzb wraps the module with the bit-oriented format "
                         "instead - the only way LZB's own crossing arm is "
                         "ever EXECUTED (SPEC.md 20.14.5). It costs ~10s of "
                         "host compression in the FIXTURE, which is why it is "
                         "not the default")
    a = ap.parse_args()

    global PACKED, IMG
    if a.fmt == "lzb":
        PACKED, IMG = "build/lzb/BEVERLY.MOD", "build/lzmodlzb360.img"
    # NO KNOB ON EITHER ARM, and that is worth stating because this row used
    # to build one. The shipped kernel carries BOTH decoders (SPEC.md
    # 20.13.6), so the only thing that differs between the arms is the
    # FIXTURE - which format the module on the scratch disk is wrapped in -
    # and the kernel that reads it is the one everybody boots. The lzb arm
    # was `make all lzmodlzbtest` in build/ followed by a bare `make` to put
    # the tree back, for a kernel that came out byte-identical either way.
    need(IMG)
    fails = []
    plain = host_checks(fails)
    P = pkg_syms("apps/tracker/tracker.asm")

    with os88marty.launch("build/os8088-360.img", apps=IMG,
                          machine=a.machine) as m:
        os88marty.settle(m, gate=os88marty.desktop_up)
        os88marty.no_saver(m)   # SPEC.md 79: it DRAWS, so the settle
                                # after the wait below can never return
                                # once it is up - and this row waits on
                                # a 116KB module. os88ui.boot does this
                                # by default and a bare launch does not
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        wins = dispcp.win_list(m, S)
        if not wins:
            sys.exit("lzmod: no Disk window after double-clicking B:")
        wx, wy = dispcp.win_rect(m, S, wins[-1])[:2]
        say("lzmod: B: lists %r" % [r[0] for r in dispcp.listing(m, S)])

        # The ASSOCIATION opens Tracker and loads the module in one action -
        # which is the requirement, not a shortcut past the File menu.
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "BEVERLY.MOD")
        # WAITED FOR, NOT SLEPT THROUGH, and the budget is the GUEST's clock -
        # so a loaded box gives the machine the same 40 seconds of its own
        # time this needs, instead of thirty host seconds of which it may get
        # twenty. The timeout is swallowed because the sentence below says
        # more about what went wrong than `until`'s does.
        try:
            os88marty.until(m,
                            lambda mm: len(dispcp.win_list(mm, S)) > len(wins),
                            "Tracker's window to open", poll=0.2, guest=40.0)
        except os88marty.MartyError:
            pass
        wins2 = dispcp.win_list(m, S)
        if len(wins2) <= len(wins):
            fails.append("no window opened: the association did not run, or "
                         "Tracker refused the module")
            return report(fails)
        rec = m.read(S("wm_wins") + wins2[-1] * dispcp.WIN_SIZE,
                     dispcp.WIN_SIZE)
        pseg = rec[22] | (rec[23] << 8)
        say("  window     Tracker at %04X" % pseg)

        # WAIT FOR THE CLAIM, do not sleep for it. [trk_modseg] going
        # non-zero IS "the module is in memory", so the read costs what the
        # guest costs and not a flat twenty seconds - and on a slow box it
        # waits LONGER rather than reading a zero and blaming the decoder,
        # which is the failure a fixed sleep has.
        def claimed(mm):
            return int.from_bytes(mm.readseg(pseg, P["trk_modseg"], 2),
                                  "little")
        try:
            os88marty.until(m, claimed, "Tracker to claim the module",
                            poll=0.2, guest=60.0)
        except os88marty.MartyError:
            pass
        os88marty.settle(m)
        modseg = claimed(m)
        if not modseg:
            fails.append("[trk_modseg] is 0: Tracker opened and holds no "
                         "module - a read that was REFUSED looks exactly like "
                         "this, so check the kernel carries this format")
            return report(fails)
        say("  module     claimed at %04X" % modseg)

        got = b""
        while len(got) < len(plain):        # 116KB, in segment-sized reads
            k = min(0x8000, len(plain) - len(got))
            got += m.readseg(modseg + (len(got) >> 4), 0, k)
        if got == plain:
            say("  bytes      ok  (all %d, byte for byte)" % len(plain))
        else:
            bad = [i for i in range(len(plain)) if got[i] != plain[i]]
            fails.append("%d of %d bytes differ, first at %d (0x%X) - which "
                         "is %s the 64KB boundary"
                         % (len(bad), len(plain), bad[0], bad[0],
                            "past" if bad[0] >= 0x10000 else "before"))

        r1 = m.readseg(pseg, P["mp_row"], 1)[0]
        os88marty.guest_sleep(m, 3.0)
        r2 = m.readseg(pseg, P["mp_row"], 1)[0]
        loaded = int.from_bytes(m.readseg(pseg, P["mp_loaded"], 2), "little")
        say("  loaded     %s  (mp_loaded=%d, row %d -> %d%s)"
            % ("ok " if loaded else "BAD", loaded, r1, r2,
               "" if r1 != r2 else " - no sound card on this machine, so the"
                                   " mixer has nothing to tick"))
        if not loaded:
            fails.append("Tracker read the bytes and did not accept them")

    return report(fails)


if __name__ == "__main__":
    sys.exit(main())
