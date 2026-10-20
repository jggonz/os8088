#!/usr/bin/env python3
"""os88vfit - a play's measured frame times against the encoder's model.

    python3 tools/os88vfit.py FILE.V88 VPPROFn.DAT [--profile NAME]
                              [--csv OUT.csv]

THE OTHER HALF OF THE PLAY'S PROFILE (VPDIAG=1's VIDEO.O88, VIDEOD.O88 on
`make vidpm`'s floppy: SPEC.md 98.3, docs/plans/LOADALL-PLAN.md L5). The
player times every frame it draws on PIT channel 2 and saves VPPROFn.DAT
beside itself; this reads that and the file it played, prices every record
the way tools/os88venc.py did when it made the file - the profile's own
table, the profile named in the file's options block unless --profile says
otherwise - and says how far the model is from the machine:

  - the DRAW (the decode, a flip's back page or page-ahead's second, the
    layer on top) against the record's model price, as a least-squares fit
    draw = a x model + b (and + c x the previous record's price on a
    flipped file, whose back page is that record again or its rows copied);
  - the WHOLE FRAME (the draw plus the record's fetch and the layer's read)
    as a share of the frame's period: mean, 95th percentile, most - what
    the file actually asked of the machine, against what the model said it
    would (the record's price over the profile's period x speed).

Version 3 (98.3.22.3) carries the INFO CARD after them - every number it
draws, and where that boot put the play: the package, the ring, the XMS
block, and the gap's own timings of the board's interleave - printed first.

A word of 0 is a frame never drawn (a seek, a stop); a draw of 55 ms or more
has wrapped channel 2 and is left out, said.

--reads holds VIDEOR's READ LOG (version 2, SPEC.md 98.3.22.2) against the
file: every chunk the player read, at its file offset, summed here the way
the player summed it as it landed in memory - a read that differs is named,
with where the first damaged super-packet was found, so a disk that hands
over the wrong bytes is told from a player that reads the right ones wrong.
"""
import argparse
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88vid as vid                                           # noqa: E402
import os88venc as V                                            # noqa: E402

PIT_US = 1e6 / 1193182.0


def read_prof(path):
    """(header, totals, draws) - and the header's `reads`, version 2's log
    as (file offset, bytes, sum), and `bad`, its damaged super-packet's
    where, or None"""
    d = open(path, "rb").read()
    if d[:4] != b"VPPR":
        raise SystemExit("%s: not a VPPROF file" % path)
    n, div = struct.unpack_from("<HH", d, 6)
    h = dict(ver=d[4], frames=n, pitdiv=div, pitper=d[10], snd=d[11],
             flip=d[12], einp=d[13], xip=d[14], lbk=d[15],
             name=d[16:29].split(b"\0")[0].decode("ascii", "replace"),
             late=struct.unpack_from("<H", d, 29)[0], reads=[], bad=None)
    if h["ver"] >= 2:
        na, nl, nb = struct.unpack_from("<HHH", d, 32)
        if nb:
            h["bad"] = dict(zip(("count", "pc", "po", "psec", "lc", "kr"),
                                (nb,) + struct.unpack_from("<5H", d, 38)))
        w = struct.unpack_from("<%dH" % (2 * n), d, 48)
        at = 48 + 4 * na
        h["reads"] = [struct.unpack_from("<IHH", d, at + 8 * i)
                      for i in range(nl)]
        h["card"] = read_card(d, at + 8 * nl) if h["ver"] >= 3 else None
    else:
        w = struct.unpack_from("<%dH" % (2 * n), d, 32)
    return h, w[0::2], w[1::2]


# VERSION 3's TRAILER (SPEC.md 98.3.22.3): the words of video.asm's
# vp_ditab, in its order, then the package's CS and SS. A byte's word
# carries its neighbour, so BYTES are masked
DI_NAMES = ("mrun0", "mfre0", "k", "rneed", "mrun", "mkeep", "msnd", "lmin",
            "lminf", "gap", "pstrm", "pause", "pmax", "pmaxf", "stall",
            "late", "dt", "done", "base", "frames", "lysp_lo", "lysp_hi",
            "lydkb", "lydln", "lytm", "lytl", "rate", "blk", "tier",
            "rshort", "kr", "xbn", "ring", "rclm", "xbase_lo", "xbase_hi",
            "xgap", "xgok", "xg_t0", "xg_bt", "xg_best", "xg0", "xg512",
            "xg1k", "xg2k", "xg4k", "xg8k", "cs", "ss")
DI_BYTES = ("rneed", "mkeep", "msnd", "tier", "rshort", "xgok")
XG_GAPS = (0, 512, 1024, 2048, 4096, 8192)


def read_card(d, at):
    """The trailer at `at` as {name: value}, or None if it is not there"""
    if d[at:at + 4] != b"VPDI":
        return None
    ln, n = struct.unpack_from("<HH", d, at + 4)
    w = struct.unpack_from("<%dH" % n, d, at + 8)
    c = dict(zip(DI_NAMES, w))
    for k in DI_BYTES:
        if k in c:
            c[k] &= 0xFF
    c["xbase"] = c.get("xbase_lo", 0) + (c.get("xbase_hi", 0) << 16)
    c["lysp"] = c.get("lysp_lo", 0) + (c.get("lysp_hi", 0) << 16)
    return c


def print_card(c, fps):
    """The info card as the player drew it, and where this boot put the
    play: the heap, the ring and the bank, and the board's interleave as
    the gap's own measurement found it (os88xgap.inc)"""
    drawn, of = c["done"] - c["base"], c["frames"] - c["base"]
    print("   THE CARD (the player's info card, saved with the profile):")
    print("     Drew %d/%d, stalls %d, pauses %d; late %d, %d ticks of %d"
          % (drawn, of, c["stall"], c["pause"], c["late"], c["dt"],
             round(drawn / fps * 18.2065)))
    if c["mfre0"]:
        print("     Heap %dK run, %dK free at Play" % (c["mrun0"],
                                                       c["mfre0"]))
    if c["mrun"]:
        print("     Ring %d/%d of %dK; keep %d snd %d%s"
              % (c["k"], c["rneed"], c["mrun"], c["mkeep"], c["msnd"],
                 " (SHORT of the file's ask)" if c["rshort"] else ""))
    if c["lmin"] != 0xFFFF:
        print("     Lead %d at f%d; hook gap %d" % (c["lmin"], c["lminf"],
                                                    c["gap"]))
    print("     Dry %d/%d stream; most %dt f%d"
          % (c["pstrm"], c["pause"], c["pmax"], c["pmaxf"]))
    if c["lysp"]:
        print("     Layer: disk %d KB/s, line %d; key %d/%d"
              % (c["lydkb"], c["lydln"], c["lytm"], c["lytl"]))
    print("     sound %d Hz, block %d; CPU tier %d; bank slots %d, ring "
          "slots in use %d" % (c["rate"], c["blk"], c["tier"], c["xbn"],
                               c["kr"]))
    print("   WHERE THIS BOOT PUT IT:")
    print("     the package at %d (CS %04X), its stack SS %04X"
          % (c["cs"] * 16, c["cs"], c["ss"]))
    if c["ring"]:
        print("     the ring (the bank's bounce) at %d, %d bytes into its "
              "claim" % (c["ring"] * 16, (c["ring"] - c["rclm"]) * 16))
    if c["xbase"]:
        print("     the XMS block at %d" % c["xbase"])
    if c["xgok"]:
        g = c["xgap"]
        ts = [c[k] for k in ("xg0", "xg512", "xg1k", "xg2k", "xg4k",
                             "xg8k")]
        if any(ts):
            t0 = ts[0] or 1
            print("     the gap's 8 KB copy up, PIT counts (x gap 0): " +
                  ", ".join("%s %d (%.2f)" % (("%dK" % (x // 1024)) if x >= 1024
                                              else str(x), t, t / t0)
                            for x, t in zip(XG_GAPS, ts)))
        if g:
            off = (c["ring"] * 16 - c["xbase"]) % (2 * g) if c["ring"] \
                else None
            print("     G = %d: the ring sits %s mod %d from the bank%s"
                  % (g, "?" if off is None else off, 2 * g,
                     "" if off is None else " - PLACED" if off == g else
                     " - NOT placed (the claim had no room for the slack?)"))
        else:
            print("     G = 0: no placement measured faster than 0 apart")
    else:
        print("     the gap was not measured this play (no XMS bank, or "
              "in place held whole)")


def rdsum(buf):
    """VIDEOR's sum (vp_prlog): each whole word, the sum rotated left once
    and the word added; an odd last byte left out"""
    s = 0
    for i in range(0, len(buf) - 1, 2):
        s = (((s << 1) | (s >> 15)) & 0xFFFF)
        s = (s + buf[i] + (buf[i + 1] << 8)) & 0xFFFF
    return s


def reads_check(path, h):
    """The read log against the file: the reads whose sum differs"""
    data = open(path, "rb").read()
    bad = []
    for i, (off, n, sm) in enumerate(h["reads"]):
        if rdsum(data[off:off + n]) != sm:
            bad.append((i, off, n, sm))
    return bad


def lstsq(rows, ys):
    """Least squares by the normal equations - no numpy needed for 2-3
    unknowns"""
    k = len(rows[0])
    ata = [[sum(r[i] * r[j] for r in rows) for j in range(k)]
           for i in range(k)]
    aty = [sum(r[i] * y for r, y in zip(rows, ys)) for i in range(k)]
    # Gauss-Jordan
    m = [ata[i] + [aty[i]] for i in range(k)]
    for c in range(k):
        p = max(range(c, k), key=lambda r: abs(m[r][c]))
        m[c], m[p] = m[p], m[c]
        if abs(m[c][c]) < 1e-12:
            return None
        for r in range(k):
            if r != c:
                f = m[r][c] / m[c][c]
                m[r] = [a - f * b for a, b in zip(m[r], m[c])]
    return [m[i][k] / m[i][i] for i in range(k)]


def q(xs, p):
    s = sorted(xs)
    return s[min(len(s) - 1, int(p * (len(s) - 1)))] if s else 0.0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("v88")
    ap.add_argument("prof")
    ap.add_argument("--profile")
    ap.add_argument("--csv")
    ap.add_argument("--reads", action="store_true")
    a = ap.parse_args()
    h, tot, drw = read_prof(a.prof)
    if a.reads:
        if h["ver"] < 2 or not h["reads"]:
            raise SystemExit("%s: no read log (VIDEOR.O88's, version 2)"
                             % a.prof)
        bad = reads_check(a.v88, h)
        rs = h["reads"]
        print("%s: %d reads logged, file offsets %d to %d; %d differ from "
              "the file" % (os.path.basename(a.prof), len(rs),
                             min(r[0] for r in rs), max(r[0] for r in rs),
                             len(bad)))
        for i, off, n, sm in bad[:12]:
            print("   read %d: %d bytes at %d (chunk %d of the file) - "
                  "sum %04X" % (i, n, off, off // 32768, sm))
        if h["bad"]:
            b = h["bad"]
            print("   damaged super-packet (%d): the decode at chunk %d "
                  "offset %d (%d sectors), the reader at chunk %d, %d "
                  "slots" % (b["count"], b["pc"], b["po"], b["psec"],
                              b["lc"], b["kr"]))
        return 1 if bad else 0
    r = vid.Reader(a.v88)
    pname = a.profile
    if pname is None:
        try:
            pname = (r.options() or {}).get("o", {}).get("profile")
        except vid.V88Error:
            pname = None
    pname = V.PROFILE_RENAMED.get(pname, pname)
    if pname not in V.PROFILES:
        raise SystemExit("no profile to price it with: --profile NAME")
    prof = dict(V.PROFILES[pname])
    g = r.g
    ct, csub = V.profile_table(prof, g.layout)
    cwm3 = V.wm3_cost(prof)
    planar = g.bitplanes or g.layout == vid.LAY_MODEX
    speed = prof.get("speed") or 1
    cyc_us = vid.HZ / 1e6 * speed           # model cycles a microsecond
    period_us = 1e6 / r.fps
    print("%s: '%s', %d frames, %s on %s, profile %s"
          % (os.path.basename(a.v88), r.title, r.frames,
             vid.PF_NAMES[r.pixfmt] if hasattr(vid, "PF_NAMES") else
             r.pixfmt, g.layout, pname))
    if h.get("card"):
        print_card(h["card"], r.fps)
    print("   played: %d frames timed of %d; sound %d, flip %d, in place "
          "%s, bank kind %d, late %d"
          % (sum(1 for x in tot if x), h["frames"], h["snd"], h["flip"],
             "bank" if h["einp"] == 2 else "EMS" if h["einp"] else
             "hold" if h["xip"] else "no", h["lbk"], h["late"]))
    def price(rec):
        return vid.cycles_of(rec, planar, table=ct, sub=csub,
                             sub2=getattr(g, "sub2", False), wm3=cwm3) \
            if planar else vid.cycles_of(rec, table=ct)
    model = []
    for rec, _sp, _i in r.records():
        if h["flip"] == 2:              # PAGE-AHEAD (98.1.3.7): the frame's
            _fl, own, hrec = vid.page_parts(rec, g)     # own record and the
            c = price(own)              # hidden page's after it, priced
            if hrec is not None:        # apart as the encoder does
                c += price(hrec)
        else:
            c = price(rec)
        model.append((c / cyc_us, len(rec)))
        if len(model) >= h["frames"]:
            break
    rows, ys, flips, out = [], [], h["flip"] == 1, []
    wrapped = 0
    for i, (t, d) in enumerate(zip(tot, drw)):
        if not t or i >= len(model):
            continue
        dus, tus = d * PIT_US, t * PIT_US
        if dus >= 54000:
            wrapped += 1
            continue
        m = model[i][0]
        pm = model[i - 1][0] if i else 0.0
        rows.append([m, pm, 1.0] if flips else [m, 1.0])
        ys.append(dus)
        out.append((i, tus, dus, m, pm, model[i][1]))
    if not rows:
        raise SystemExit("no frame timed")
    fit = lstsq(rows, ys)
    res = [y - sum(c * x for c, x in zip(fit, rr)) for rr, y in zip(rows, ys)]
    rms = (sum(e * e for e in res) / len(res)) ** 0.5
    if flips:
        print("   the draw = %.3f x model + %.3f x the last record's + %.0f us"
              " (rms %.0f us over %d frames)" % (fit[0], fit[1], fit[2], rms,
                                                  len(rows)))
    else:
        print("   the draw = %.3f x model + %.0f us (rms %.0f us over %d "
              "frames)" % (fit[0], fit[1], rms, len(rows)))
    ratio = [o[2] / o[3] for o in out if o[3] > 2000]
    if ratio:
        print("   measured/model, records past 2 ms of model: median %.3f, "
              "10%% %.3f, 90%% %.3f" % (q(ratio, .5), q(ratio, .1),
                                         q(ratio, .9)))
    share = [o[1] / period_us for o in out]
    mshare = [(o[3] + (o[4] if flips else 0)) / period_us for o in out]
    print("   the whole frame, of a %.1f ms period: mean %.1f%%, 95%% %.1f%%,"
          " most %.1f%% (frame %d)"
          % (period_us / 1000, 100 * sum(share) / len(share),
             100 * q(share, .95), 100 * max(share),
             out[max(range(len(share)), key=share.__getitem__)][0]))
    print("   ...as the model priced the draw: mean %.1f%%, 95%% %.1f%%, "
          "most %.1f%%" % (100 * sum(mshare) / len(mshare),
                           100 * q(mshare, .95), 100 * max(mshare)))
    if wrapped:
        print("   %d frames ran 54 ms or more (channel 2 wrapped): left out"
              % wrapped)
    if a.csv:
        with open(a.csv, "w") as f:
            f.write("frame,total_us,draw_us,model_us,prev_model_us,bytes\n")
            for o in out:
                f.write("%d,%.0f,%.0f,%.0f,%.0f,%d\n" % o)
    return 0


if __name__ == "__main__":
    sys.exit(main())
