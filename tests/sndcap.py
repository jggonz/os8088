#!/usr/bin/env python3
"""SPEC.md 34.6.4: verb 4 records into an EXTERNAL ring (SND_OPENF_EXT).

    make && make sndcaptest && python3 tests/sndcap.py

tests/sndcap/sndcap.asm runs itself off its window timer and writes every
answer it got into a RESULTS block (magic 'SCAPRES1'); this reads the block
out of the package's segment. It boots MartyPC's 5150 with a Sound Blaster -
a DSP 2.01, auto-init, whose ADC records a flat 80h - on the mixer gate's
360KB system disk (its SYSTEM.CFG wants SOUND.DRV), opens B:\\SNDCAP.O88 by
name, and asserts:

  1. a ring whose physical span crosses a 64KB page answers 7;
  2. verb 4 EXT on a 16KB OSAPI_MEM_CLAIM_DMA_HI ring at 11,025 Hz opens
     (AX = 0), and zeroes SND_EXT_CONS, seeded BEEFh, at the open;
  3. after ~2 s the published count is the rate times the ticks, in WHOLE
     2,048-byte blocks, and verb 3 reads PLAYING with the same count - no
     overrun pause, though nothing ever read the ring;
  4. the card wrote the ring: every byte 80h where it was filled with 00h;
  5. verb 2 answers 0 and STOPS it: 10 ticks after the close the count has
     not moved and a ring zeroed again is still all 00h;
  6. the grant-and-drain record stream is GONE (verb 4 without
     SND_OPENF_EXT): it answers 7 and opens nothing - the external ring is
     the one record path;
  7. made a single-cycle DSP ([sbl_auto] = 0, poked by this script, which is
     the only way MartyPC's DSP 2.01 can be one), verb 4 EXT answers 7.
"""
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tools"), os.path.join(ROOT, "tests")]
import os88build                                        # noqa: E402
import os88geom as G                                    # noqa: E402

PKG = "build/sndcap.bin"
MAGIC = b"SCAPRES1"
RATE = 11025
BLOCK = 2048
RL = 16384
TICK = 18.2065
NRES = 60

bad = []


def check(cond, what):
    print(("  ok   " if cond else "  FAIL ") + what, flush=True)
    if not cond:
        bad.append(what)


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


def res_offset():
    d = open(os88build.at(PKG), "rb").read()
    o = d.find(MAGIC)
    if o < 0:
        raise SystemExit("sndcap: no %r in %s" % (MAGIC, PKG))
    return o


FIELDS = ("caps", None, "r1", "xopen", "cons0", "count", "st", "stdx", "t0",
          "t1", "xclose", "cons1", "n80", "n00", "w0", "wn", "cons2", "q00",
          "lopen", "lst", "ldx", "lrd", "lw", "lclose", "r1x")


def parse(blk):
    r = {"done": blk[10], "state": blk[11], "go": blk[58]}
    for k, name in enumerate(FIELDS):
        if name:
            r[name] = u16(blk, 8 + 2 * k)
    return r


def main():
    import os88drvsym
    import os88marty
    import os88ui
    off = res_offset()
    auto = os88drvsym.symbols("sound.bin")["sbl_auto"]
    with os88ui.boot("build/mixer360.img", apps="build/sndcap360.img",
                     machine=os88marty.machine("os8088_5150_sb_gla"),
                     verbose=False) as ui:
        m = ui.m
        w = ui.path("B:/SNDCAP.O88")
        raw = m.read(m.sym("wm_wins"), G.MAX_WIN * G.WIN_SIZE)
        seg = u16(raw, w.i * G.WIN_SIZE + G.W_SEG)
        print("SNDCAP in segment %04x" % seg)
        if m.readseg(seg, off, len(MAGIC)) != MAGIC:
            raise SystemExit("sndcap: no results block in that segment")
        # the external leg, its quiet and the legacy leg, then the wait
        os88marty.until(m, lambda _: m.readseg(seg, off + 11, 1)[0] >= 5,
                        "the run to reach its go wait", limit=120)
        dseg = u16(m.read(m.sym("drv_tab") + 2, 2)) << 4   # SOUND.DRV, row 0
        was = m.read(dseg + auto, 1)
        print("sbl_auto was %d; made 0 for the single-cycle leg" % was[0])
        m.write(dseg + auto, b"\x00")
        m.write((seg << 4) + off + 58, b"\x01")
        os88marty.until(m, lambda _: m.readseg(seg, off + 10, 1)[0] == 1,
                        "the run's end", limit=60)
        m.write(dseg + auto, was)
        r = parse(m.readseg(seg, off, NRES))
    report(r, was[0])


def report(r, was_auto):
    print(" ".join("%s=%04x" % (k, r[k]) for k in FIELDS if k))
    ticks = (r["t1"] - r["t0"]) & 0xFFFF
    want = RATE * ticks / TICK
    print("captured %d bytes in %d ticks: %.0f a second (want %d)"
          % (r["count"], ticks, r["count"] * TICK / max(ticks, 1), RATE))
    check(was_auto == 1, "the driver had an auto-init DSP to begin with "
          "(sbl_auto = %d)" % was_auto)
    check(r["caps"] & 0x10, "an auto-init DSP advertises SND_CAP_PCM_IN "
          "(caps %04x)" % r["caps"])
    check(r["r1"] == 7, "a ring across a 64KB page answers 7 (%04x)" % r["r1"])
    check(r["xopen"] & 0xFF == 0, "verb 4 EXT opened the external ring "
          "(%04x)" % r["xopen"])
    check(r["cons0"] == 0, "the open zeroed SND_EXT_CONS, seeded BEEFh "
          "(%04x)" % r["cons0"])
    check(r["count"] % BLOCK == 0, "the count is whole %d-byte blocks (%d)"
          % (BLOCK, r["count"]))
    check(ticks > 0 and abs(r["count"] - want) <= BLOCK + 0.05 * want,
          "the count is the rate times the ticks: %d for %.0f, within a "
          "block" % (r["count"], want))
    check(r["st"] == 0, "verb 3 reads PLAYING - no overrun pause, though "
          "nothing read the ring (%d)" % r["st"])
    check(r["stdx"] == r["count"], "verb 3's count is the published one "
          "(%d against %d)" % (r["stdx"], r["count"]))
    check(r["n80"] == RL and r["n00"] == 0,
          "the card wrote the whole ring: %d bytes 80h, %d still 00h "
          "(first word %04x, last %04x)" % (r["n80"], r["n00"], r["w0"],
                                            r["wn"]))
    check(r["xclose"] == 0, "verb 2 answered 0 (%04x)" % r["xclose"])
    check(r["cons2"] == r["cons1"], "10 ticks after the close the count "
          "has not moved (%d, %d)" % (r["cons1"], r["cons2"]))
    check(r["q00"] == RL, "...and a ring zeroed after the close is still "
          "all 00h (%d)" % r["q00"])
    check(r["lopen"] == 7, "verb 4 into a grant - the old record stream - "
          "answers 7 and opens nothing (%04x)" % r["lopen"])
    check(r["go"] == 1 and r["r1x"] == 7, "a single-cycle DSP answers verb 4 "
          "EXT with 7 (%04x)" % r["r1x"])


if __name__ == "__main__":
    main()
    if bad:
        print("sndcap: %d FAILED" % len(bad))
        sys.exit(1)
    print("sndcap: all passed")
