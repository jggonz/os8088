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
          "ART INDK SND").split()
# CSP_SND is SPEC.md 88.8.2's engine voice, and it is the LAST row of every
# record (apps/skies/csworld.inc). It was added without this list, and the cost
# is worth stating because it is the opposite of a loud failure: the order
# check below fails and then `continue`s, so EVERY arithmetic check in this
# file - the DRAGK/THRUST balance at VMAX, the 3/4-VMAX acceleration, the
# induced term at the stall - was SKIPPED for all five records while the row
# reported five failures about field order. A row that stops testing its own
# subject is the shape docs/WRITING-TESTS.md 1 is about, so a new field wants
# appending here in the same commit that adds it.
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


INDMAX = 64                     # CS_INDMAX, skies.asm


def drag_at(v, dragk, indk=0, vstall=0):
    """The model's own drag, integer for integer (csflight.inc's cs_step).

    TWO TERMS SINCE 88.7.12, and the second is why this function grew an
    argument rather than staying the parasitic half: induced drag rises as
    the aeroplane slows, so a VMAX balance struck against the parasitic term
    alone over-states how much thrust is left at the top end. It is small
    there - single figures against a thrust of 14 to 60 - and it is the whole
    of the drag at 1.1 x the stall, which is where a landing happens.
    """
    q = (v * v) >> 16
    d = (q * dragk) >> 16
    if indk:
        d += min(indk // max(q, (vstall * vstall) >> 16, 1), INDMAX)
    return d


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
        indk, vstall = f["INDK"], f["VSTALL"]
        # THE WING'S OWN DRAG IS FLOORED AT THE STALL (88.7.12) and capped,
        # or an aeroplane at taxi speed would be pinned by a term that runs
        # to infinity as v goes to zero
        check(indk >= 0 and (thrust == 0 or indk > 0),
              "%s: CSP_INDK is declared (%d)" % (name, indk))
        if indk:
            qs = (vstall * vstall) >> 16
            check(indk // max(qs, 1) <= INDMAX,
                  "%s: the induced term at the stall is %d, inside CS_INDMAX "
                  "%d" % (name, indk // max(qs, 1), INDMAX))
        if thrust == 0:                     # the sailplane: gravity is its
            check(dragk > 0,                # engine and DRAGK its glide ratio
                  "%s has no engine, so DRAGK is its GLIDE ratio (%d)"
                  % (name, dragk))
            continue
        d = drag_at(vmax, dragk, indk, vstall)
        check(thrust - 1 <= d <= thrust,
              "%s: DRAGK %d and INDK %d balance THRUST %d at VMAX %d - the "
              "drag there is %d" % (name, dragk, indk, thrust, vmax, d))
        # ...and it is a BALANCE and not a coincidence of small numbers: at
        # three quarters of VMAX the aeroplane must still be accelerating
        d34 = drag_at(vmax * 3 // 4, dragk, indk, vstall)
        check(d34 < thrust,
              "%s: and it still accelerates at 3/4 VMAX (drag %d of thrust "
              "%d)" % (name, d34, thrust))
        # ...AND IT MUST BE ABLE TO FLY SLOWLY (88.7.12). The induced term
        # rises as the aeroplane slows and the whole point of it is that
        # holding 1.1 x the stall costs real thrust - but if it costs MORE
        # than the engine has, the aeroplane cannot be flown onto a runway
        # at all, which is the regression the first build of it shipped
        # before the stall floor was added: every throttle from 10% to 100%
        # settled at 2 m/s.
        dslow = drag_at(vstall * 11 // 10, dragk, indk, vstall)
        check(dslow < thrust,
              "%s: and it holds 1.1 x the stall (drag %d of thrust %d)"
              % (name, dslow, thrust))
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
