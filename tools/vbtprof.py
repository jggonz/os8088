#!/usr/bin/env python3
"""A Video Battle play's PROFILE, read (SPEC.md 126.8): the VBPROFn.DAT
that VBATTLED.O88 writes beside itself when a play ends.

    python3 tools/vbtprof.py VBPROF1.DAT [--bundle BATTLEV.VBT] [--frames]

What it says, per clip and for the play: how long the hook's DECODE took
against the frame's period (a frame of decode longer than the period is
time the sound and the reader did not get), the frames STALLED (the next
record not in its ring - the disk behind), the frames LATE (forgiven: the
picture a second behind the sound), how far the clip's ring was read
ahead, the card's sound lead at its lowest (near 0: the sound breaks up),
the hook's sound feed's time, and the reads - their KB, their time and the
rate they came at. --bundle names the clips; --frames lists every frame.
"""
import argparse
import struct
import sys

PIT_US = 1e6 / 1193182.0        # a PIT count
MODES = {0: "off", 1: "clip", 2: "idle", 3: "hold", 4: "fade", 5: "owait"}
CLIP_MODE = 1           # (vbattle.asm's VM_* values)


def read_prof(path):
    d = open(path, "rb").read()
    if d[:4] != b"VBPR":
        raise SystemExit("%s: not a VBPROF file" % path)
    (ver, nf, nr, div, fps, rate, spf, kind, k, late, stall, hper, rper,
     rbytes, rcalls) = struct.unpack_from("<HHHHHHHBBHHIIIH", d, 4)
    name = d[38:51].split(b"\0")[0].decode("ascii", "replace")
    snd, rkb, pre, prmax, ckb = d[51], struct.unpack_from("<H", d, 52)[0], \
        struct.unpack_from("<H", d, 54)[0], \
        struct.unpack_from("<H", d, 56)[0], struct.unpack_from("<H", d, 58)[0]
    h = dict(ver=ver, frames=nf, reads=nr, div=div, fps=fps, rate=rate,
             spf=spf, kind=("CGA4", "VGA4", "Hercules")[kind]
             if kind < 3 else kind, k=k, late=late, stall=stall, hper=hper,
             rper=rper, rbytes=rbytes, rcalls=rcalls, name=name, card=snd,
             ringkb=rkb, room=prmax, claimkb=ckb, xms=d[60] == 1,
             dropped=struct.unpack_from("<H", d, 62)[0])
    fr = []
    for i in range(nf):
        o = 64 + pre * i
        t, gap, fl, clip, ahead, lead, feed = struct.unpack_from(
            "<HHBBBBH", d, o)
        fr.append(dict(t=t * PIT_US / 1000, gap=gap * PIT_US / 1000,
                       stall=bool(fl & 1), mode=fl >> 4, clip=clip,
                       ahead=ahead, lead=lead * 256, feed=feed * PIT_US /
                       1000))
    rd = []
    base = 64 + pre * prmax
    per_ms = div * PIT_US / 1000
    for i in range(nr):
        p0, dp, n, off = struct.unpack_from("<HHHH", d, base + 8 * i)
        rd.append(dict(at=p0 * per_ms / 1000, ms=dp * per_ms, kb=n / 1024.0,
                       off=off * 4096))
    return h, fr, rd


def clip_names(path):
    import os
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import os88vbt as B
    return [c["name"] for c in B.Bundle(open(path, "rb").read()).clips]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("prof")
    ap.add_argument("--bundle")
    ap.add_argument("--frames", action="store_true")
    a = ap.parse_args()
    h, fr, rd = read_prof(a.prof)
    names = clip_names(a.bundle) if a.bundle else None
    period = 1000.0 / h["fps"] if h["fps"] else 0
    print("%s: %s, %d fps (a frame %.1f ms), sound %d Hz%s, rings %d KB" % (
        h["name"], h["kind"] + (" %d colours" % h["k"] if h["k"] else ""),
        h["fps"], period, h["rate"], " on a card" if h["card"] else
        " (no card)", h["ringkb"]))
    secs = h["hper"] * h["div"] * PIT_US / 1e6
    if h["xms"]:
        where = "kept in XMS (%d KB)%s" % (h["claimkb"], "; %d frames the "
                                            "staging dropped" % h["dropped"]
                                            if h["dropped"] else "")
    else:
        where = "room for %d in a %d KB claim%s" % (
            h["room"], h["claimkb"], ": the rest of the play was not"
            if len(fr) >= h["room"] else "")
    print("the play: %.1f s, %d frames profiled (%s), %d stalled, %d late "
          "(forgiven)" % (secs, len(fr), where, h["stall"], h["late"]))
    print("the reader: %d calls, %.0f KB, %.1f s of the hook's periods "
          "(%.0f KB/s while reading)" % (
              h["rcalls"], h["rbytes"] / 1024.0,
              h["rper"] * h["div"] * PIT_US / 1e6,
              h["rbytes"] / 1024.0 / max(1e-6, h["rper"] * h["div"] * PIT_US
                                         / 1e6)))
    print()
    by = {}
    for f in fr:
        key = (MODES.get(f["mode"], f["mode"]), f["clip"])
        by.setdefault(key, []).append(f)
    print("%-16s %6s %8s %8s %6s %6s %8s %8s" % (
        "what", "frames", "decode", "worst", "over", "stall", "ahead",
        "lead min"))
    for (m, c), fs in sorted(by.items(), key=lambda x: str(x[0])):
        nm = m if c == 255 else (names[c] if names and c < len(names)
                                 else "clip %d" % c)
        ts = [f["t"] for f in fs if not f["stall"]]
        print("%-16s %6d %6.1fms %6.1fms %6d %6d %6dKB %7dB" % (
            nm[:16], len(fs), sum(ts) / max(1, len(ts)), max(ts or [0]),
            sum(1 for t in ts if t > period), sum(f["stall"] for f in fs),
            min(f["ahead"] for f in fs), min(f["lead"] for f in fs)))
    feeds = [f["feed"] for f in fr]
    if feeds:
        print("\nthe sound feed: %.1f ms a frame on average, %.1f at most" % (
            sum(feeds) / len(feeds), max(feeds)))
    if rd:
        tot = sum(r["kb"] for r in rd)
        ms = sum(r["ms"] for r in rd)
        print("reads logged: %d, %.0f KB in %.1f s - %.0f KB/s; the slowest "
              "%.0f ms (%.1f KB)" % (len(rd), tot, ms / 1000, tot / max(
                  1e-6, ms / 1000), max(r["ms"] for r in rd),
                  max(rd, key=lambda r: r["ms"])["kb"]))
    bad = sorted(range(len(fr)), key=lambda i: -fr[i]["t"])[:5]
    if bad:
        print("\nthe slowest frames: " + ", ".join(
            "#%d %.1f ms" % (i, fr[i]["t"]) for i in bad))
    if a.frames:
        print("\n%6s %-6s %4s %8s %8s %5s %6s %7s %7s" % (
            "frame", "mode", "clip", "decode", "gap", "stall", "ahead",
            "lead", "feed"))
        for i, f in enumerate(fr):
            print("%6d %-6s %4s %6.1fms %6.1fms %5s %4dKB %6dB %5.1fms" % (
                i, MODES.get(f["mode"], f["mode"]),
                "" if f["clip"] == 255 else f["clip"], f["t"], f["gap"],
                "yes" if f["stall"] else "", f["ahead"], f["lead"],
                f["feed"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
