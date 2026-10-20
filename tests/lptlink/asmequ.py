"""The cable's constants, read out of the assembly that defines them.

    from asmequ import equ
    NET_SOCKS = equ("drivers/net/netpkg.inc", "NET_SOCKS")

partner.py and linksim.py are the host-side halves of a protocol whose
contract is asm: netpkg.inc, nwire.inc, net.asm, lplink.inc. They used to TYPE
those numbers out, with a comment saying where each came from - and a comment
is not a check. `NET_SOCKS = 4` sat in partner.py after netpkg.inc moved to 8,
and tests/socktest.py failed on it ("8 of 4 handles free") with the driver
behaving perfectly. tests/unit/t_mirror.py could not see it, because its
Python side is a hand-written list plus os88geom's names, and none of these
are on either.

So nothing is mirrored any more: every value is read from its asm `equ` at
import, and a name that has gone from the source is an ImportError naming the
file, not a stale number.
"""
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))


def _value(text):
    if len(text) == 3 and text[0] == text[2] == "'":
        return ord(text[1])                     # a character: 'X'
    return int(text, 0)                         # 8, 0x04, 0xF483


def equ(rel, *names):
    """`NAME equ VALUE` out of ROOT/rel: one value for one name, else a list.

    Only a plain number or a quoted character is understood - an `equ` whose
    value is an expression is refused rather than guessed at."""
    with open(os.path.join(ROOT, rel), errors="replace") as f:
        src = f.read()
    out = []
    for name in names:
        m = re.search(r"^%s\s+equ\s+('.'|\S+)" % re.escape(name), src, re.M)
        if not m:
            raise ImportError("%s has no `%s equ` - the cable's host side "
                              "reads its constants from there" % (rel, name))
        try:
            out.append(_value(m.group(1)))
        except ValueError:
            raise ImportError("%s: `%s equ %s` is not a plain number"
                              % (rel, name, m.group(1)))
    return out[0] if len(out) == 1 else out
