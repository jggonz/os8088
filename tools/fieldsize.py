#!/usr/bin/env python3
"""fieldsize.py SHIPPED.bin FIELD.bin - do the two kernels occupy the same rung?

A benchmark kernel is not bound by KERN_BUDGET: the field machines all have
640KB and the only thing that limits an instrument is the RAM in the box. So
instruments may be compiled into the field kernels freely, and DISKCNT=1 now
is.

What that freedom does NOT buy is measurement parity, and two rows of a
sysbench report are measurements OF THE KERNEL THAT IS RUNNING rather than of
the machine:

  boot ticks              the kernel is read off the floppy a sector at a
                          time, so a kernel one rung bigger is 512 more bytes
                          of boot - and PERFORMANCE.md Part 9 Set 18's
                          726 -> 181 series was taken on this disk

  claim heap KB           the heap starts where the kernel ends, so every
  largest free run KB     free-memory figure, and the size of the largest
  total free KB           package that will load, moves with it

Everything else - the drawing primitives, the CPU rows, RAM bandwidth, and
the floppy's own bytes/second - is untouched by a kernel that grew, because
none of it is measured against the kernel's own extent.

The unit that matters is KIMG_PARA's 512-byte rung and not the byte count:
the ladder rounds, so two kernels in the same rung produce an IDENTICAL memory
map and an identical sector count, and their rows are comparable exactly.
This says which case you are in. It is a report and never an error - growing
past a rung is allowed, it just has to be known about rather than discovered
later in a number that moved for no visible reason.
"""
import sys, os

def rung(n):
    return (n + 511) // 512

def main():
    if len(sys.argv) != 3:
        print("usage: fieldsize.py SHIPPED.bin FIELD.bin", file=sys.stderr)
        return 2
    shipped, field = sys.argv[1], sys.argv[2]
    for p in (shipped, field):
        if not os.path.exists(p):
            print(f"fieldsize: {p} does not exist - skipping the check")
            return 0
    a, b = os.path.getsize(shipped), os.path.getsize(field)
    ra, rb = rung(a), rung(b)
    if ra == rb:
        print(f"fieldsize: field kernel {b} bytes vs shipped {a} - "
              f"same {ra}-sector rung, so boot ticks and the heap rows "
              f"are exactly comparable")
        return 0
    print(f"fieldsize: NOTE - field kernel {b} bytes ({rb} sectors) vs "
          f"shipped {a} ({ra} sectors).")
    print(f"fieldsize: the instruments cost {rb - ra} sector(s), which is "
          f"allowed (a bench kernel has no KERN_BUDGET) but DOES move")
    print(f"fieldsize: `boot ticks` and every heap row. Do not compare those "
          f"against a set taken on a different rung.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
