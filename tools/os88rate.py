#!/usr/bin/env python3
"""os88rate - can the mixer HOLD a sample rate on the machine under it?

    make trklog
    python3 tools/os88rate.py --rates 0,2                 # windowed
    python3 tools/os88rate.py --rates 0,2 --fullscreen    # the text screen

WHY THIS EXISTS. PERFORMANCE.md Set 20 took Tracker's XT mixer from 45.2% of a
4.77MHz 8088 to 29.6% and ended on the honest question it could not answer:
what is the leftover capacity FOR? The only large lever on this mixer is the
sample rate - the work is `channels x rate` and nothing else - so the question
is always "does rate R still fit", and it is not a question a profile answers
on its own. A profile says where the time went; it does not say whether the
music came out.

SO THE HEADLINE NUMBER IS NOT THE PROFILE. It is BYTES PER SECOND REACHING THE
CARD, `d[trk_consumed] / d(guest cycles)`. The card asks for `rate` bytes a
second and, when the ring cannot supply them, SPEC.md 34.5's honest path takes
bounded SILENCE rather than looping stale samples (SBL_ST_UNDER) - so this
ratio IS the fraction of the music that was audible, and 83% means 17% of the
song was never played. The ring lead (`trk_total - trk_consumed`) is the same
story one step earlier: full ring = the worker is ahead, collapsing = losing.

Guest CYCLES and not host time, because the host schedules the emulator and a
busy container would otherwise read as a starved 8088.

FOUR THINGS THAT COST A RUN EACH, all of them silent:

  * A DRIVER IS NOT "OUTSIDE THE PACKAGE". A .DRV is a v4 package whose image
    is a heap claim taken from the TOP down (SPEC.md 51), so its code sits in
    HIGH memory - and a naive `base <= ip < base + 0x10000` package window
    swallows it and buckets it into whatever the last label in the listing
    happens to be. The first run of this put SOUND.DRV's block ISR in
    `tlog_s_clkr`, which is a STRING; the giveaway was that a string scaled
    with the sample rate. Every loaded driver gets a bucket of its own here
    and the package window stops at the image size.
  * THE IMAGE SIZE IS AT HEADER +8. +4 is the link base and is always 0, so
    reading it there yields a window of zero and every sample lands outside.
  * K IS WINDOWED-ONLY (docs/FIELD-NOTES.md 16), so the rate is set BEFORE the
    fullscreen bracket is entered. Pressing it inside is a key the app refuses
    and a loop that never ends.
  * THE BSS NAMES ARE `equ`s, emitted by TRKB/TRKW, so they carry no label
    line at all: their addresses are scraped out of the ENCODED [addr] operand
    of any instruction that names them (tools/os88avsync.py's method).

The machine wants a Sound Blaster in it, which in a container means
`os8088_5150_sb_gla` - the IBM-ROM `os8088_5150_sb` needs a ROM this tree
cannot ship.
"""
import sys, os, re, time, subprocess, argparse

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
os.chdir(ROOT)
import os88marty, os88mouse, os88sym, dispcp                      # noqa: E402

GUEST_HZ = 4772728.0
LST = "/tmp/os88rate.lst"
RATE_NAME = {0: "5,500 (XT mode's own)", 1: "4,000", 2: "11,000"}


def symbols(defines=("TRKLOG",)):
    """(name -> offset) for labels, and '@name' -> offset for the bss equs."""
    cmd = ["nasm", "-f", "bin", "-w+error", "-I", "apps/", "-I", "apps/tracker/",
           "-I", "tests/", "-o", "/tmp/os88rate.bin", "-l", LST]
    cmd += ["-D" + d for d in defines] + ["apps/tracker/tracker.asm"]
    subprocess.run(cmd, check=True)
    out, syms, pending, last = {}, [], [], None
    lines = open(LST).read().splitlines()
    for L in lines:
        m = re.match(r"\s*\d+\s+([0-9A-F]{8})\s", L)
        addr = int(m.group(1), 16) if m else None
        # NASM marks INCLUDED lines with <1>, <2>... and leaves the top-level
        # file unmarked - so a pattern anchored on <1> sees every .inc and
        # none of tracker.asm. That was silent in both consumers: the gate
        # could not find `trk_e_rate`, and no `trk_*` symbol had ever appeared
        # in a profile, which reads as "the app spends no time in its own main
        # file" rather than as a missing pattern.
        #
        # Both patterns are ANCHORED at the start of the source text and the
        # COMMENT is cut off first, because a loose `\w+:` search anywhere in
        # the line harvests English: the first version of this fix put `es`
        # at 44% of the machine, `pass` at 8.7% and `DX` at 1.9%, all of them
        # words in comments, and the profile still looked like a profile.
        body = L.split(";", 1)[0]
        lab = re.search(r"<\d+>\s*(\.?[A-Za-z_][A-Za-z0-9_.]*):", body) or \
            re.match(r"\s*\d+\s+(?:[0-9A-F]{8}\s+(?:[0-9A-F\[\]()<>-]+\s+)?)?"
                     r"(\.?[A-Za-z_][A-Za-z0-9_.]*):", body)
        if lab:
            raw = lab.group(1)
            n = (last or "?") + raw if raw.startswith(".") else raw
            if not raw.startswith("."):
                last = n
            if addr is not None:
                syms.append((addr, n))
            else:
                pending.append(n)
            continue
        if addr is not None and pending:
            syms += [(addr, n) for n in pending]
            pending = []
    for a, n in syms:                     # image labels, by name, so a caller
        out.setdefault(n, a)              # can walk a structure the kernel
                                          # walks (tests/trkrate.py)
    for L in lines:                       # the bss equs, by encoded operand
        mo = re.search(r"\[([0-9A-F]{2})([0-9A-F]{2})\]", L)
        if not mo:
            continue
        for nm in re.findall(r"\[([a-z_][a-z0-9_]*)\]", L):
            out.setdefault("@" + nm, int(mo.group(2) + mo.group(1), 16))
    syms.sort()
    return out, syms


def scan(m):
    """(tracker segment, [(driver name, segment, image bytes)])."""
    buf = m.read(0x40000, 0xA0000 - 0x40000)
    seg, drv = None, []
    for o in range(0, len(buf) - 32, 16):
        if buf[o:o + 2] != b"O8":
            continue
        ver, sz = buf[o + 2], int.from_bytes(buf[o + 8:o + 10], "little")
        name = buf[o + 16:o + 32].split(b"\0")[0].decode("latin1").strip()
        if ver == 3 and name == "TRACKER":
            seg = (0x40000 + o) >> 4
        elif ver == 4:
            drv.append((name, (0x40000 + o) >> 4, sz))
    return seg, drv


def kernel_syms():
    """(linear address, name) for every kernel symbol, sorted.

    Needed because "outside the package" is not one thing: on the fullscreen
    surface it is mostly the SCHEDULER, and a run that reports 17% idle while
    the audio starves is saying something completely different from a run that
    is out of cycles. `.cold`, `.ovl` and `.lowbss` each live somewhere other
    than KERNEL_SEG, so the segment is asked per symbol.
    """
    out = []
    for n in os88sym.syms():
        try:
            out.append((os88sym.linear(n), n))
        except Exception:
            pass
    out.sort()
    return out


def bucket(syms, off):
    lo, hi, best = 0, len(syms) - 1, syms[0][1]
    while lo <= hi:
        mid = (lo + hi) // 2
        if syms[mid][0] <= off:
            best, lo = syms[mid][1], mid + 1
        else:
            hi = mid - 1
    return best


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_sb_gla")
    ap.add_argument("--apps", default="build/trklog360.img",
                    help="the disk with TRKLOG.O88 and a module on it")
    ap.add_argument("--defines", default="TRKLOG",
                    help="the FULL nasm -D list the disk was built with, so "
                         "an empty string means the SHIPPED player and not "
                         "'TRKLOG plus nothing' - the prepend that used to be "
                         "implicit here assembled a bench listing against a "
                         "shipped binary and read mp_xt as 254. THE "
                         "LISTING MUST MATCH THE BINARY: a knob that moves the "
                         "image by five bytes moves every bss equ with it, and "
                         "the reads then land on the wrong words - which reads "
                         "as XT mode being off on a machine that armed it")
    ap.add_argument("--secs", type=float, default=60.0,
                    help="sampling span per rate, in HOST seconds")
    ap.add_argument("--rates", default="0,2",
                    help="K indices: 0 = 5,500  1 = 4,000  2 = 11,000")
    ap.add_argument("--fullscreen", action="store_true",
                    help="measure on SPEC.md 45.13's text screen instead")
    ap.add_argument("--rkey", action="store_true",
                    help="drive the rate with R (SPEC.md 45.9.3's shipped "
                         "control, 5,500 <-> 11,000) instead of the bench "
                         "build's K. This is the only way to measure the "
                         "SHIPPED player, which has no K at all - and the "
                         "rate pick is behind an %ifdef, so the bench build "
                         "is not evidence about it")
    a = ap.parse_args()
    a.defines_given = any(x.startswith("--defines") for x in sys.argv)

    # `make trkrate` writes the defines beside each disk, so the commonest
    # way to get a wrong answer here - a listing that describes a DIFFERENT
    # binary, which reads as "XT mode is not armed" on a machine that armed
    # it - is one the tool can rule out rather than the caller remembering.
    side = os.path.splitext(a.apps)[0] + ".defines"
    if os.path.exists(side) and not a.defines_given:
        a.defines = open(side).read().strip()
        print("defines from %s: %r" % (side, a.defines))
    P, syms = symbols(tuple(
        d for d in a.defines.replace(",", " ").split() if d))
    ksyms = kernel_syms()
    S = os88sym.linear

    with os88marty.launch("build/os8088-360.img", apps=a.apps,
                          machine=a.machine, boot=False) as m:
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        slot = dispcp.win_list(m, S)[-1]
        wx, wy, _, _ = dispcp.win_rect(m, S, slot)
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "BEVERLY.MOD")

        seg = None
        for _ in range(60):
            time.sleep(2)
            seg, drv = scan(m)
            if seg:
                break
        if not seg:
            print("FAIL: Tracker never loaded"); return 1
        time.sleep(25)                    # 116KB of module off a 360KB floppy
        base = seg * 16
        imgend = int.from_bytes(m.read(base + 8, 2), "little")

        def A(n):
            if "@" + n not in P:
                raise SystemExit("no address for %s in the listing" % n)
            return P["@" + n]
        w = lambda n: int.from_bytes(m.read(base + A(n), 2), "little")
        b = lambda n: m.read(base + A(n), 1)[0]

        seg, drv = scan(m)
        print("TRACKER @%04X, image %d, mp_xt=%d; drivers: %s"
              % (seg, imgend, b("mp_xt"),
                 ", ".join("%s @%04X+%d" % d for d in drv) or "none"))
        if not b("mp_xt"):
            print("NOTE: XT mode is NOT armed - this is not an XT-mode figure")

        for ki in [int(x) for x in a.rates.split(",")]:
            if a.rkey:                    # R: 0 = 5,500, 2 = 11,000, no 4,000
                want = 1 if ki == 2 else 0
                for _ in range(3):
                    if b("trk_xhi") == want:
                        break
                    m.key("KeyR"); time.sleep(1.5)
                else:
                    raise SystemExit("R never reached trk_xhi %d" % want)
            else:
                for _ in range(6):        # K is windowed-only: set it FIRST
                    if b("tlog_xr") == ki:
                        break
                    m.key("KeyK"); time.sleep(1.0)
                else:
                    raise SystemExit("K never reached index %d" % ki)
            if a.fullscreen:
                m.key("KeyF"); time.sleep(4)
            m.key("Enter")                # play
            time.sleep(12)                # ...past the pre-roll (SPEC.md 45.18)

            rate = w("mp_mixrate")
            c0, t0 = w("trk_consumed"), m.cmd(cmd="status")["cycles"]
            hits, tot, leads, wraps, last = {}, 0, [], 0, c0
            t = time.time()
            while time.time() - t < a.secs:
                ip = m.cmd(cmd="status")["flat_ip"]
                tot += 1
                for n, sg, sz in drv:
                    if sg * 16 <= ip < sg * 16 + sz:
                        hits["[%s]" % n] = hits.get("[%s]" % n, 0) + 1
                        break
                else:
                    if base <= ip < base + imgend:
                        k = bucket(syms, ip - base)
                    elif ksyms and ksyms[0][0] <= ip:
                        k = "k:" + bucket(ksyms, ip)
                    else:
                        k = "(BIOS / below the kernel)"
                    hits[k] = hits.get(k, 0) + 1
                if tot % 20 == 0:
                    cc = w("trk_consumed")
                    if cc < last:
                        wraps += 1        # free-running 16-bit, mod 65536
                    last = cc
                    leads.append((w("trk_total") - cc) & 0xFFFF)
            secs = (m.cmd(cmd="status")["cycles"] - t0) / GUEST_HZ
            bps = ((w("trk_consumed") + wraps * 65536) - c0) / secs
            mix = sum(c for n, c in hits.items() if n.startswith("mp_"))

            print("\n=== %s Hz, %s ==="
                  % (rate, "fullscreen text" if a.fullscreen else "windowed"))
            print("  %.1f s of guest, %d samples" % (secs, tot))
            print("  AUDIBLE      %.0f B/s of %d asked = %.1f%%%s"
                  % (bps, rate, 100.0 * bps / rate,
                     "" if bps / rate > 0.99 else "   <-- STARVING"))
            print("  ring lead    min %d  median %d  max %d  (of %d)"
                  % (min(leads), sorted(leads)[len(leads) // 2], max(leads),
                     max(leads)))
            print("  replayer+mixer (every mp_*)   %.1f%%" % (100.0 * mix / tot))
            cat = {}
            for n, c in hits.items():
                if n.startswith("mp_"):
                    g = "the mixer + replayer"
                elif n.startswith("[") :
                    g = "the sound DRIVER"
                elif n.startswith("ttx_") or n.startswith("tui_"):
                    g = "the text screen"
                elif n.startswith("k:sch_") or n.startswith("k:task_"):
                    g = "the SCHEDULER"
                elif n.startswith("k:"):
                    g = "the rest of the kernel"
                else:
                    g = "the rest of the package"
                cat[g] = cat.get(g, 0) + c
            for g, c in sorted(cat.items(), key=lambda kv: -kv[1]):
                print("    %-28s %5.1f%%" % (g, 100.0 * c / tot))
            print("  ...by symbol:")
            for n, c in sorted(hits.items(), key=lambda kv: -kv[1])[:18]:
                print("    %-28s %5.1f%%" % (n, 100.0 * c / tot))
            m.key("Space"); time.sleep(3)         # stop, so K can take effect
            if a.fullscreen:
                m.key("Escape"); time.sleep(4)    # ...and windowed, so K works
    return 0


if __name__ == "__main__":      # ...so tests/trkrate.py can import
    sys.exit(main())               # symbols() and scan() without
                                   # running a whole measurement
