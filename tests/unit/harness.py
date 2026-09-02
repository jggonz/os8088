"""The two dozen lines of test harness the host-side checks share.

Deliberately not `unittest`: every check in here is a statement about the
BINARY this tree just built, and what makes such a failure actionable is the
numbers on either side of it - the offset, the byte, the symbol it resolved
to - not a stack trace.  So a failing check prints one line saying what was
expected and what was found, every check runs even after one fails (a
renumbering breaks fifty slots and the useful report is all fifty, not the
first), and the file exits 1 if any did.

`why=` is not decoration.  A check that fails months from now is read by
somebody who does not know what it was defending, and this tree's own
history is the argument: the API table's silent collision (docs/UPSTREAM.md)
looks like nothing at all in a diff.  Say what breaks.
"""
import sys

_FAILED = []
_PASSED = [0]


def _brief(v, limit=160):
    """A value short enough to read.

    Written after the first run of t_pkg compared two 12KB driver images and
    printed both of them: 20,000 characters of hex for one failing row, which
    buries the eight rows underneath it. A binary is summarised by its length,
    its hash and the offset of the FIRST difference - which is the part
    somebody can actually act on.
    """
    if isinstance(v, (bytes, bytearray)) and len(v) > limit:
        import hashlib
        return "<%d bytes, md5 %s>" % (len(v), hashlib.md5(v).hexdigest())
    s = v if isinstance(v, str) else repr(v)
    return s if len(s) <= limit else s[:limit] + "... (%d more)" % (len(s) - limit)


def _where(got, want):
    """The offset of the first differing byte, for two blobs."""
    if not (isinstance(got, (bytes, bytearray)) and isinstance(want, (bytes, bytearray))):
        return ""
    n = min(len(got), len(want))
    for i in range(n):
        if got[i] != want[i]:
            return "\n        first difference at offset %d (0x%X)" % (i, i)
    return "\n        identical for %d bytes, then the lengths differ" % n


def check(cond, what, why="", got=None, want=None):
    """Record one assertion. Never raises - see the module docstring."""
    if cond:
        _PASSED[0] += 1
        return True
    msg = what
    if want is not None or got is not None:
        msg += "\n        want: %s\n        got:  %s" % (_brief(want), _brief(got))
        msg += _where(got, want)
    if why:
        msg += "\n        why:  %s" % why
    _FAILED.append(msg)
    return False


def eq(got, want, what, why=""):
    return check(got == want, what, why, got=got, want=want)


def done(title):
    """Print the verdict and exit. Call this at the bottom of every file."""
    if _FAILED:
        print("%s: %d checks passed, %d FAILED" % (title, _PASSED[0], len(_FAILED)))
        for m in _FAILED:
            print("  FAIL: %s" % m)
        sys.exit(1)
    print("%s: %d checks passed" % (title, _PASSED[0]))
    sys.exit(0)
