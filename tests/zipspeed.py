#!/usr/bin/env python3
"""zipspeed - SPEC.md 138.9, docs/plans/ZIP-PLAN.md claim 9: what a sector costs.

    make zipimg && python3 tests/zipspeed.py --arm vpi0|vpi2|ps2-0|ps2-2

The number SPEC.md 138.9 prints is the GUEST'S time to read and to write a
sector through the shipped driver on a 4.77 MHz XT, nibble mode, and this is
how it is taken. MartyPC's Zip drive keeps the guest's own clock (it is told
how long every instruction took) and adds up, per READ and per WRITE command,
the time from the CONNECT to the last byte of the message: selection, the CDB,
the data phase, status and message - everything the transport does and
nothing the driver does between commands. `zip_stats` hands back `read_us`,
`write_us` and the command counts; sectors come from `sectors_read` and
`sectors_written`, and a 64 KB figure is 128 sectors of them.

The guest does the work through the REAL path: Edit > Copy of BIG200.BIN (a
200,000-byte file, so a run of full bursts and a tail) off the Zip onto B:,
and Edit > Copy of a 120,001-byte file from B: back onto it. It is not a
loop in the guest and not a breakpoint timer, so what it measures is what a
user's copy costs the transport - including the kernel's FAT window refills.

It GATES very little, on purpose: a number that decides nothing should not
fail a row. The rate must be above 1 KB/s (a hang and a slow drive are
different findings) and the counters must say a transfer happened. What it
does is print the figures and write them to build/zipspeed-<arm>.txt for the
final pass to copy into 138.9. A MartyPC figure is a GUEST CYCLE figure: it
has no model of the drive's own mechanics, so it bounds the PORT from below.
"""
import argparse
import os
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tools"), os.path.join(ROOT, "tests")]
os.chdir(ROOT)
import os88flush                                           # noqa: E402,F401
import os88ui                                              # noqa: E402
import os88zip                                             # noqa: E402
import zipdrive as Z                                       # noqa: E402
import zipq                                                # noqa: E402

ARMS = {k: Z.ARMS[k] for k in ("vpi0", "vpi2", "ps2-0", "ps2-2")}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--arm", required=True, choices=sorted(ARMS))
    a = ap.parse_args()
    machine, disk, mode, size, _big, ps2 = ARMS[a.arm]
    tmp = tempfile.mkdtemp(prefix="zipspeed-")
    img = zipq.scratch_copy(disk, tmp)
    bimg = os.path.join(tmp, "b.img")
    Z.mkfloppy(bimg, {"FROMB.BIN": os88zip.pattern(120001, 0xB0B)})
    with os88ui.boot(Z.BOOT, apps=bimg, machine=machine, zip=img, verbose=False) as ui:
        m = ui.m
        row, seg, err, new = zipq.tick_zip(ui)
        if not (seg and not err and len(new) == 1):
            sys.exit("zipspeed: ZIP.DRV did not load (segment %#x, error %d, zones %r)" % (seg, err, new))
        L = new[0]
        m.zip_stats(reset=True)
        Z.copy(ui, L, "BIG200.BIN", "B")
        r = m.zip_stats()
        Z.copy(ui, "B", "FROMB.BIN", L)
        w = m.zip_stats()
    rd_s, wr_s = r["sectors_read"], w["sectors_written"]
    if not (rd_s and wr_s and r["read_us"] and w["write_us"]):
        sys.exit("zipspeed: no transfer was timed: %r" % {k: w[k] for k in
                 ("sectors_read", "sectors_written", "read_us", "write_us", "read_cmds", "write_cmds")})
    # the second snapshot is cumulative: the write leg is what the copy back added
    rd_us = r["read_us"]
    wr_us = w["write_us"]
    rd_ms_sec = rd_us / rd_s / 1000.0
    wr_ms_sec = wr_us / wr_s / 1000.0
    out = ["zipspeed[%s] %s, %s (%s)" % (a.arm, machine, mode, "PS/2" if ps2 else "nibble"),
           "  read : %6d sectors in %4d commands, %10.1f ms of guest time = %6.2f ms/sector"
           % (rd_s, r["read_cmds"], rd_us / 1000.0, rd_ms_sec),
           "         64 KB (128 sectors) = %7.1f ms   %6.1f KB/s" % (128 * rd_ms_sec, 0.5 / rd_ms_sec * 1000.0),
           "  write: %6d sectors in %4d commands, %10.1f ms of guest time = %6.2f ms/sector"
           % (wr_s, w["write_cmds"], wr_us / 1000.0, wr_ms_sec),
           "         64 KB (128 sectors) = %7.1f ms   %6.1f KB/s" % (128 * wr_ms_sec, 0.5 / wr_ms_sec * 1000.0)]
    print("\n".join(out))
    dst = os.path.join(zipq.BUILD, "zipspeed-%s.txt" % a.arm)
    with open(dst, "w") as f:
        f.write("\n".join(out) + "\n")
    if 0.5 / rd_ms_sec * 1000.0 < 1.0 or 0.5 / wr_ms_sec * 1000.0 < 1.0:
        sys.exit("zipspeed: under 1 KB/s - that is a hang, not a speed")
    if w["violations"]:
        sys.exit("zipspeed: the driver left %d protocol violations" % w["violations"])
    print("zipspeed[%s]: measured (written to %s)" % (a.arm, dst))


if __name__ == "__main__":
    main()
