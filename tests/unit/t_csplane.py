#!/usr/bin/env python3
"""CLEAR SKIES' plane records agree with their own drag (SPEC.md 88.7.4).

    python3 tests/unit/t_csplane.py

`CSP_DRAGK` is not a free number: the flight model integrates
`v += thrust - drag` with `drag = ((v*v) >> 16) * DRAGK >> 16`, so DRAGK is
what decides where the aeroplane stops accelerating - and every record's
comment says "balances THRUST at VMAX". A THRUST changed without re-deriving
DRAGK moves the top speed instead, silently and by a lot, and no flight test
in the suite is long enough to notice: an A5 at 1.5 m/s^2 takes 42 seconds of
GUEST time to reach 95 knots.

So the arithmetic is checked here, on the source, in milliseconds. It is the
row that caught nothing when it was written and exists for the next person to
change a thrust - which is exactly the change the field asked for in the A5.

It also holds the speeds in order (stall < rotate < max), with the sailplane
exempt: its CSP_VROT is deliberately unreachable, a sailplane that lands
having landed (88.7.6).
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "apps", "skies", "csworld.inc")
FIELDS = ("NAME VSTALL VROT VMAX THRUST DRAGK FRICT BRAKE ROLLR ROLLL PITCHR "
          "PITCHT TURNK EYE MAXROLL MAXPITCH COCKPIT ATT SPOOL LAUNCH FLAGS "
          "ART").split()
bad = []


def check(cond, what):
    print("  [%s] %s" % ("PASS" if cond else "FAIL", what))
    if not cond:
        bad.append(what)


def records(text):
    """Every cs_p_* record as {field: value}; the words in declaration order.

    The rows are `dw <expr>` one a line with the field named in the comment,
    so the VALUE is parsed and the NAME is only checked - a record that grew
    a row would otherwise be read silently off by one from there down.
    """
    out = {}
    for m in re.finditer(r"^(cs_p_\w+):\s*$(.*?)(?=^\S|\Z)", text,
                         re.M | re.S):
        name, body = m.group(1), m.group(2)
        vals, names = [], []
        for line in body.splitlines():
            g = re.match(r"\s*dw\s+([^;]+?)\s*(?:;\s*CSP_(\w+).*)?$", line)
            if not g:
                continue
            expr = g.group(1).strip()
            names.append(g.group(2))
            try:
                vals.append(eval(expr, {"__builtins__": {}}, {}))
            except Exception:
                vals.append(None)           # a symbol: not a number we check
        out[name] = (vals, names)
    return out


def drag_at(v, dragk):
    """The model's own drag, integer for integer (csflight.inc's cs_step)."""
    return (((v * v) >> 16) * dragk) >> 16


def main():
    text = open(SRC).read()
    recs = records(text)
    check(len(recs) >= 5, "every plane record is found (%d)" % len(recs))
    for name, (vals, names) in sorted(recs.items()):
        got = [n for n in names if n]
        check(got == FIELDS,
              "%s declares the fields in CSP_ order (%s)"
              % (name, "ok" if got == FIELDS else got))
        if got != FIELDS:
            continue
        f = dict(zip(FIELDS, vals))
        vmax, thrust, dragk = f["VMAX"], f["THRUST"], f["DRAGK"]
        if thrust == 0:                     # the sailplane: gravity is its
            check(dragk > 0,                # engine and DRAGK its glide ratio
                  "%s has no engine, so DRAGK is its GLIDE ratio (%d)"
                  % (name, dragk))
            continue
        d = drag_at(vmax, dragk)
        check(thrust - 1 <= d <= thrust,
              "%s: DRAGK %d balances THRUST %d at VMAX %d - the drag there is "
              "%d" % (name, dragk, thrust, vmax, d))
        # ...and it is a BALANCE and not a coincidence of small numbers: at
        # three quarters of VMAX the aeroplane must still be accelerating
        d34 = drag_at(vmax * 3 // 4, dragk)
        check(d34 < thrust,
              "%s: and it still accelerates at 3/4 VMAX (drag %d of thrust "
              "%d)" % (name, d34, thrust))
        check(f["VSTALL"] < f["VMAX"],
              "%s: stall %d is below VMAX %d" % (name, f["VSTALL"], f["VMAX"]))
        if f["LAUNCH"] == 0:
            check(f["VSTALL"] < f["VROT"] < f["VMAX"],
                  "%s: stall %d < rotate %d < max %d"
                  % (name, f["VSTALL"], f["VROT"], f["VMAX"]))
    if bad:
        for b in bad:
            print("FAIL: " + b)
        return 1
    print("  ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
