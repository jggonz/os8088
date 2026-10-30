#!/usr/bin/env python3
"""SPEC.md 20.1.1: a package of EXACTLY `APP_MAX_SIZE` loads, runs and closes.

    make pkgbig && python3 tests/pkgmax.py [machine] [system-image]

`APP_MAX_SIZE` was 0xF000 - the retired package pool's size - and is 0xFE00
now, the largest multiple of 512 a word holds. Over 63KB the region is a whole
**64KB claim**, and that is the size two of the loader's 16-bit words wrapped
on before the raise:

  * `mem_bytes_kb_x` was `add ax, 1023 / shr` and answered **0 KB** for every
    byte count from 64,513 up - a claim with nowhere to put the image;
  * `ld_alloc`'s `I_SIZE = KB << 10` is 65,536 for 64KB, which is **0** in a
    word - and 0 is how the instance table says BUILT-IN, so the instance
    would be swept as one.

`PKGMAX.O88` (tests/pkgbig/pkgmax.asm) is image + bss = 0xFE00 to the byte.
Its entry proc writes 0xA5 into the LAST byte of its bss if the loader left a
0 there, so one read shows the region reaches 0xFDFF and was zeroed.

FIVE ASSERTIONS, all off guest state: `ld_status` is LD_OK and the window
opened; the instance is a PACKAGE with `I_SIZE` = 0xFFFF (saturated, not 0);
the region's claim is 64KB = 4,096 paragraphs owned by that instance; the
mark is at segment:0xFDFF; and after the close box, the instance and every
claim on that segment are gone.

BROKEN ON PURPOSE, both halves, and both go red: with the `sbb ax, 0` out of
`ld_alloc` the instance reads `I_SIZE` 0; with `mem_bytes_kb_x` put back to
`add ax, 1023` the claim is 0KB and the load does not survive.
"""
import os
import struct
import sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
import os88geom                                         # noqa: E402
import os88marty                                        # noqa: E402
import os88ui                                           # noqa: E402
from os88pkg import APP_MAX_SIZE                        # noqa: E402

# pkgfence's machine and disks: build/pkgbig.img exists at 1.44MB only.
MACHINE = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_herc_gla_144"
SYS_IMG = sys.argv[2] if len(sys.argv) > 2 else "build/os8088-360.img"
APPS_IMG = "build/pkgbig.img"

LD_OK = 0
PM_MARK = 0xA5                  # tests/pkgbig/pkgmax.asm's
KIND_PKG = os88geom.KIND_PKG
I_RECSZ, I_KIND, I_SPTR, I_SIZE = (os88geom.I_RECSZ, os88geom.I_KIND,
                                   os88geom.I_SPTR, os88geom.I_SIZE)
MC_SIZE, MEM_MAX = os88geom.MC_SIZE, os88geom.MEM_MAX


def u16(b, o=0):
    return struct.unpack_from("<H", b, o)[0]


def claims(m, S):
    """(seg, paragraphs, owner) for every used mem_tab record."""
    raw = m.read(S("mem_tab"), MEM_MAX * MC_SIZE)
    return [(u16(raw, i * MC_SIZE), u16(raw, i * MC_SIZE + 2),
             u16(raw, i * MC_SIZE + 4))
            for i in range(MEM_MAX) if u16(raw, i * MC_SIZE)]


MEGA = bool(os.environ.get("OS88_MEGA"))     # kern_mega under Bochs


def pool(m, S):
    """kern_mega's pool blocks as (KB offset, KB, owner) - mx_tab in
    kernel/mega.inc: MX_OFF, MX_KB, MX_OWN, 96 records of 10 bytes."""
    mx = m.read(S("mx_tab"), 96 * 10)
    return [(u16(mx, k), u16(mx, k + 2), mx[k + 4])
            for k in range(0, len(mx), 10) if u16(mx, k + 2)]


def main():
    if not os.path.exists(APPS_IMG):
        sys.exit("pkgmax: %s is missing - run `make pkgbig`" % APPS_IMG)
    fails = []
    with os88ui.boot(SYS_IMG, apps=APPS_IMG, machine=MACHINE,
                     quick=True) as ui:
        ui.still(0)     # booted on STATE; the first paint done (SOAK-SPEED 4)
        m, S = ui.m, ui.sym
        w = ui.path("B:/PKGMAX.O88")
        st = m.read(S("ld_status"), 1)[0]
        print("  ld_status %d, window %r in slot %d" % (st, w.title, w.i))
        if st != LD_OK:
            fails.append("ld_status %d, want LD_OK" % st)

        slot = m.read(S("wm_owner") + w.i, 1)[0]
        rec = m.read(S("inst_tab") + slot * I_RECSZ, I_RECSZ)
        kind, seg, size = rec[I_KIND], u16(rec, I_SPTR), u16(rec, I_SIZE)
        print("  instance %d: kind %#04x, region %04x, I_SIZE %#06x"
              % (slot, kind, seg, size))
        if not kind & KIND_PKG:
            fails.append("instance %d is not a package (kind %#04x)"
                         % (slot, kind))
        if size != 0xFFFF:
            fails.append("I_SIZE is %#06x, want 0xFFFF: a 64KB region is "
                         "64 << 10 = 0 in a word, and 0 is a BUILT-IN "
                         "(SPEC.md 20.1.1)" % size)

        mine = [c for c in claims(m, S) if c[0] == seg]
        if MEGA:
            # kern_mega names the region by an LDT SELECTOR: a conventional
            # claim's mem_tab record keeps its real PARAGRAPH (the selector
            # in mem_sel beside it), and a claim above 1MB is a POOL block
            # owned 80h|slot with no record at all (KERN-MEGA-MEMORY-PLAN 1.2,
            # MC7.4) - the same question either way, in the same units
            d = m.selector(seg)
            para = d[0] >> 4 if d else -1
            mine = [c for c in claims(m, S) if c[0] == para] + \
                [(seg, kb * 64, own & 0x7F) for _, kb, own in pool(m, S)
                 if own == 0x80 | slot]
            print("  region selector %04x: %r" % (seg, d))
            if not d or d[1] != 0xFFFF:
                fails.append("the region's selector is %r, want a 64KB limit "
                             "(0xFFFF)" % (d,))
        print("  claims on %04x: %r" % (seg, mine))
        if len(mine) != 1 or mine[0][1] != 0x1000 or mine[0][2] != slot:
            fails.append("the region's claim is %r, want one record of "
                         "4,096 paragraphs (64KB) owned by instance %d"
                         % (mine, slot))

        lin = getattr(m, "lin", lambda s_, o_=0: (s_ << 4) + o_)
        mark = m.read(lin(seg, APP_MAX_SIZE - 1), 1)[0]
        print("  last bss byte (%04x:%04x) = %#04x"
              % (seg, APP_MAX_SIZE - 1, mark))
        if mark != PM_MARK:
            fails.append("the last bss byte reads %#04x, want %#04x: the "
                         "entry proc found it non-zero or never ran"
                         % (mark, PM_MARK))

        ui.close(w)
        # A closed instance is DYING (state 2) until the UI task's next pass
        # tears it down (SPEC.md 29.2), so wait for FREE on the guest's clock
        try:
            os88marty.until(m, lambda _m: _m.read(S("inst_tab") + slot
                                                  * I_RECSZ, 1)[0] == 0,
                            "instance %d to go free" % slot, poll=0.3,
                            limit=30)
        except os88marty.MartyError:
            pass                        # the assertion below names it
        rec = m.read(S("inst_tab") + slot * I_RECSZ, I_RECSZ)
        left = [c for c in claims(m, S) if c[0] == seg]
        if MEGA:
            left += [c for c in claims(m, S) if c[0] == para]
            left += [b for b in pool(m, S) if b[2] == 0x80 | slot]
        print("  after close: instance state %d, claims on %04x %r"
              % (rec[0], seg, left))
        if rec[0] != 0 or left:
            fails.append("closing left instance state %d and claims %r"
                         % (rec[0], left))

    if fails:
        print("\npkgmax: FAIL")
        for f in fails:
            print("  " + f)
        return 1
    print("\npkgmax: a package of exactly APP_MAX_SIZE (%d) loads into a "
          "64KB claim, reads I_SIZE 0xFFFF and closes clean - PASS on %s"
          % (APP_MAX_SIZE, MACHINE))
    return 0


if __name__ == "__main__":
    sys.exit(main())
