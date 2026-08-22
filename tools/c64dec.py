#!/usr/bin/env python3
"""c64dec.py - the decimal-mode reference for C64's 6510 core gate.

    python3 tools/c64dec.py            print the four -D defines
    python3 tools/c64dec.py --show     ...and a few named cases, for a human

Klaus Dormann's `6502_decimal_test` is published as SOURCE only - the
project's `bin_files/` carries the FUNCTIONAL test's binary and the 65C02
one, and nothing else - so C64-SPEC §4.6's decimal row cannot be a
fetched binary at a pinned hash the way its functional row is. This file is
what replaces it, and it is a stronger check rather than a weaker one:

  * it is an INDEPENDENT implementation, written here from the documented
    NMOS 6502 decimal rules and not from apps/c64/c64cpu.inc - the same
    posture tools/c64ref.py takes toward the composer;
  * it covers ALL 262,144 cases - ADC and SBC, every accumulator, every
    operand, carry in both ways - where Dormann's own decimal test covers
    the same space;
  * the answer it hands the harness is four 16-bit checksums, because
    262,144 expected results will not fit in a boot sector and any single
    wrong result in any sweep changes the checksum it belongs to.

THE RULES, and where each comes from. In decimal mode the NMOS 6502 is not
the CMOS one and the difference is the FLAGS:

  ADC   Z comes from the BINARY sum (a + m + c) & 0xFF - not from the decimal
        result. N and V come from the high nibble BEFORE its decimal
        adjustment. C comes from after it.
  SBC   every flag is the BINARY subtraction's - N, V, Z and C alike - and
        only the accumulator is adjusted.

The checksum is the harness's, exactly: for each case, fold
`(A << 8) | (P & 0xC3)` into a 16-bit accumulator that is rotated left one
bit first - an ADD and not an exclusive-or, because an XOR fold over a
symmetric sweep of exactly 65,536 cases cancels itself to zero and would have
made all four checksums the same number. The sweep order is the harness's too - the accumulator is the
outer loop and the operand the inner one - because a checksum is only a
comparison if both sides visit the cases in the same order.
"""
import sys


def adc_dec(a, m, c):
    """ADC in decimal mode -> (result, N, V, Z, C)."""
    t = (a & 0x0F) + (m & 0x0F) + c
    if t > 9:
        t += 6
    h = (a >> 4) + (m >> 4) + (1 if t > 0x0F else 0)
    z = 1 if ((a + m + c) & 0xFF) == 0 else 0       # the BINARY sum
    hi = (h << 4) & 0xFF
    n = 1 if (hi & 0x80) else 0
    v = 1 if ((a ^ hi) & 0x80) and not ((a ^ m) & 0x80) else 0
    if h > 9:
        h += 6
    c_out = 1 if h > 0x0F else 0
    r = (((h & 0x0F) << 4) | (t & 0x0F)) & 0xFF
    return r, n, v, z, c_out


def sbc_dec(a, m, c):
    """SBC in decimal mode -> (result, N, V, Z, C). Every FLAG is binary."""
    borrow = 1 - c
    b = a - m - borrow
    r_bin = b & 0xFF
    n = 1 if (r_bin & 0x80) else 0
    z = 1 if r_bin == 0 else 0
    c_out = 1 if b >= 0 else 0
    v = 1 if ((a ^ m) & (a ^ r_bin) & 0x80) else 0
    t = (a & 0x0F) - (m & 0x0F) - borrow
    hb = 0
    if t & 0x10:
        t = (t - 6) & 0x0F
        hb = 1
    else:
        t &= 0x0F
    h = (a >> 4) - (m >> 4) - hb
    if h & 0x10:
        h -= 6
    r = (((h & 0x0F) << 4) | t) & 0xFF
    return r, n, v, z, c_out


def pbits(n, v, z, c):
    return (0x80 if n else 0) | (0x40 if v else 0) | (0x02 if z else 0) | (1 if c else 0)


def sweep(op, cin):
    """The harness's own fold, in the harness's own order."""
    s = 0
    for a in range(256):
        for m in range(256):
            r, n, v, z, c = op(a, m, cin)
            word = ((r & 0xFF) << 8) | pbits(n, v, z, c)
            s = ((s << 1) | (s >> 15)) & 0xFFFF     # rol ax, 1
            s = (s + word) & 0xFFFF                 # add ax, bx
    return s & 0xFFFF


def main():
    names = ("DECADC0", "DECADC1", "DECSBC0", "DECSBC1")
    vals = (sweep(adc_dec, 0), sweep(adc_dec, 1),
            sweep(sbc_dec, 0), sweep(sbc_dec, 1))
    if "--show" in sys.argv:
        for a, m, c in ((0x00, 0x00, 0), (0x99, 0x01, 0), (0x50, 0x50, 0),
                        (0x00, 0x01, 0), (0x12, 0x34, 1)):
            print("ADC $%02X + $%02X + %d = $%02X  P=$%02X"
                  % ((a, m, c) + (lambda t: (t[0], pbits(*t[1:])))(adc_dec(a, m, c))))
            print("SBC $%02X - $%02X - %d = $%02X  P=$%02X"
                  % ((a, m, 1 - c) + (lambda t: (t[0], pbits(*t[1:])))(sbc_dec(a, m, c))))
    print(" ".join("-D%s=0x%04X" % (n, v) for n, v in zip(names, vals)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
