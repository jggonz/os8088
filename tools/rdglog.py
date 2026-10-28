#!/usr/bin/env python3
"""Read RealDMG's Machine > Log (docs/REALDMG-SPEC.md section 14.1) - one
log, or two lined up at their PLAY+ - in the F1 line's terms.

    python3 tools/rdglog.py KIRBY-00.LOG               every second, a summary
    python3 tools/rdglog.py A.LOG B.LOG                two side by side
    python3 tools/rdglog.py --brief A.LOG [B.LOG]      the summary alone

The log is the instrument a field machine and an emulator both have: the same
replay (section 10.5) played with Log on writes the same seconds on each, and
PLAY+ marks the second the replay began, so the owner's 86Box and this
tree's line up second for second without anybody timing anything. Each row
is what the F1 line showed that second - SPEED (frames run against the
59.7275 a second a Game Boy runs, the line's first number) and FPS (frames
drawn a second, its second) - and what the log adds: where the second went
(CPU, ms running the Game Boy, and DRAW, ms drawing - near 1,000 between
them is the machine's limit), the frames OWED at the worst (how far behind
the machine got; 30 is the cap), the BLOCKS the recompiler
translated, its FLUSHES (a recycled tier counts as one, 16.2.3) and the BANKS
the bank cache fetched (10.3).

The summary says what the eye sees: how often the speed fell below 70% and
below 90%, the longest such run, and the bursts over 105% that catch up
after one (a machine that fell behind runs the owed frames fast).
"""
import argparse
import re
import signal
import sys

LINE = re.compile(r"^\s*(\d+)\.(\d)\s+(\d+)%\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)"
                  r"\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)"
                  r"\s+(\d+) ([WF])(.*)$")
FIELDS = ("run", "drawn", "comp", "chg", "draw", "scan", "cmp", "cpu", "max",
          "owed", "evts", "blk", "fl", "bk")


def parse(path):
    """-> (head lines, records): each record a dict, its seconds a float"""
    head, recs, prev = [], [], 0.0
    for raw in open(path, "rb").read().decode("latin-1").splitlines():
        m = LINE.match(raw)
        if not m:
            if not recs and raw.strip():
                head.append(raw.rstrip())
            continue
        r = {"t": int(m.group(1)) + int(m.group(2)) / 10.0,
             "speed": int(m.group(3))}
        for i, k in enumerate(FIELDS):
            r[k] = int(m.group(4 + i))
        r["mode"] = m.group(18)
        rest = m.group(19).split()
        r["flags"] = rest[0] if rest and re.match(r"^[JI]+$", rest[0]) else ""
        r["events"] = [w for w in rest if w != r["flags"]]
        r["span"] = max(r["t"] - prev, 0.1)
        r["fps"] = r["drawn"] / r["span"]
        prev = r["t"]
        recs.append(r)
    return head, recs


def from_play(recs):
    """the records from the one PLAY+ is in (the whole log without one)"""
    for i, r in enumerate(recs):
        if "PLAY+" in r["events"]:
            return recs[i:]
    return recs


def summary(recs):
    sp = [r["speed"] for r in recs]
    if not sp:
        return "no records"
    low70 = sum(1 for s in sp if s < 70)
    low90 = sum(1 for s in sp if s < 90)
    hi = sum(1 for s in sp if s > 105)
    run = best = 0
    for s in sp:
        run = run + 1 if s < 70 else 0
        best = max(best, run)
    fps = [r["fps"] for r in recs]
    return ("%d s: speed mean %.0f%%, least %d%%, most %d%%; under 70%% %d s "
            "(longest run %d s), under 90%% %d s, over 105%% %d s; drawn %.1f fps "
            "mean (least %.0f, most %.0f); owed at most %d; %d blocks, %d flushes, "
            "%d banks fetched"
            % (len(sp), sum(sp) / len(sp), min(sp), max(sp), low70, best, low90, hi,
               sum(fps) / len(fps), min(fps), max(fps),
               max(r["owed"] for r in recs), sum(r["blk"] for r in recs),
               sum(r["fl"] for r in recs), sum(r["bk"] for r in recs)))


def row(r):
    if r is None:
        return " " * 46
    return "%4d%% %3.0ffps %4d %4d %4d %4d %3d %3d" % (
        r["speed"], r["fps"], r["cpu"], r["draw"], r["owed"], r["blk"],
        r["fl"], r["bk"])


def main():
    signal.signal(signal.SIGPIPE, signal.SIG_DFL)   # (| head)
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("logs", nargs="+")
    ap.add_argument("--brief", action="store_true", help="the summary alone")
    ap.add_argument("--all", action="store_true",
                    help="every record, not only from PLAY+")
    a = ap.parse_args()
    if len(a.logs) > 2:
        sys.exit("rdglog: one log, or two")
    logs = []
    for p in a.logs:
        head, recs = parse(p)
        logs.append((p, head, recs if a.all else from_play(recs)))
    for p, head, recs in logs:
        print("== %s" % p)
        for h in head[:2]:
            print("   " + h)
        print("   " + summary(recs))
    if a.brief:
        return 0
    cols = "speed  F1fps  cpu draw owed  blk  fl  bk"
    print()
    print("  sec  " + "   |   ".join(cols for _ in logs) + "   events")
    n = max(len(r) for _, _, r in logs)
    for i in range(n):
        rs = [recs[i] if i < len(recs) else None for _, _, recs in logs]
        ev = " ".join(sorted(set(e for r in rs if r for e in r["events"])))
        print("%5d  %s   %s" % (i, "   |   ".join(row(r) for r in rs), ev))
    return 0


if __name__ == "__main__":
    sys.exit(main())
