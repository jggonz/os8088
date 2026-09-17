#!/usr/bin/env python3
"""RAD tunes that must be refused, and exactly how (SPEC.md 34.12.1, 96.4.4).

    python3 tests/unit/t_rad.py

Every byte of a tune came off a floppy, and Reality's players check nothing,
so SOUND.DRV's verb 4 validates the whole file before its first use and
`tools/radsim.py` implements the same rules to the byte.  This is the
hostile-file table the two are held to: each row is a file built HERE, by
hand, from SPEC.md 96.4 - not by radsim's fixture composer - and the
`(RADE_*, RADC_*, offset)` triple SPEC.md says it earns.  The offsets are
written as positions in the file each row builds, so a reader can check one
against the procedure in 96.4.4 without running anything.

Wave 3 points the same table at the driver (a `-DRADLOG` build on MartyPC);
until then the row that can disagree with this table is radsim.

A row that is ACCEPTED is also played for a few hundred frames by radsim's
engine for its version: an accepted file that makes the reference replayer
read outside the tune is a hole in the rules, whatever the triple says. And
one accepted file is built to fan out, to hold a frame's WORK to RAD_PNMAX
(SPEC.md 96.4.5) - with a negative control that takes the cap out.
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
from harness import check, eq, done                      # noqa: E402
import radsim                                            # noqa: E402

SIG = b"RAD by REALiTY!!"

# SPEC.md 34.12's codes and 96.4.4's details, by name, as the SPEC numbers them
NOTRAD, VERSION, CORRUPT, NEEDOPL3, BIG = 1, 2, 3, 4, 5
TRUNC, FLAGS, BPM, INST, MIDI, ORDLEN, JUMP, ORDER, PATNUM, PATOFF = range(1, 11)
PTRUNC, EXTRA, LINE, CHAN, NOTE, INSNUM, EFFECT, RIFFID = range(11, 19)
OK = None


def bad(detail, offset):
    return (CORRUPT, detail, offset)


# --- 2.1 -------------------------------------------------------------------
# The smallest valid 2.1 tune, byte by byte:
#   00..0F signature, 10 version 21h, 11 flags 00h, 12 description's 00h,
#   13 instrument list's 00h, 14 order length 1, 15 order entry 0,
#   16 pattern list's FFh, 17 riff list's FFh.                 n = 18h
def v2(flags=0, bpm=None, desc=b"", insts=b"", orders=b"\x00", pats=b"", riffs=b"",
       tail=b""):
    out = SIG + b"\x21" + bytes((flags,))
    if bpm is not None:
        out += struct.pack("<H", bpm)
    out += desc + b"\0" + insts + b"\0" + bytes((len(orders),)) + orders
    return out + pats + b"\xff" + riffs + b"\xff" + tail


def track(body):
    return struct.pack("<H", len(body)) + body


def fm(num, name=b"", alg=0, riff=None):
    head = bytes((num, len(name))) + name
    return head + bytes(((0x80 if riff is not None else 0) | alg, 0, 1, 60)) + bytes(20) + \
        (track(riff) if riff is not None else b"")


def midi(num, version=0):
    return bytes((num, 0, 7, 0, version << 4 | 4, 0x2C, 0, 0, 0x40))


V2_MIN = v2()
# a one-line pattern 0: line 80h (last, line 0), entry 81h (last, channel 1)
PAT0 = b"\x00" + track(b"\x80\x81")
P = 0x16                      # where the pattern list starts in v2() with defaults


def v2pat(body):
    """V2_MIN with pattern 0 = `body`; the track's bytes start at P + 3."""
    return v2(pats=b"\x00" + track(body))


ROWS = []


def row(name, data, want, opl3=True):
    ROWS.append((name, data, want, opl3))


row("2.1 minimal", V2_MIN, OK)
row("2.1 one-entry pattern", v2(pats=PAT0), OK)
row("under 17 bytes", SIG, (NOTRAD, 0, 0))
row("17 bytes, bad signature", b"RAD by REALiTY!?" + b"\x21", (NOTRAD, 0, 0))
row("version 20h", SIG + b"\x20" + V2_MIN[17:], (VERSION, 0, 0))
row("version 11h", SIG + b"\x11" + V2_MIN[17:], (VERSION, 0, 0))
row("2.1 on an OPL2", V2_MIN, (NEEDOPL3, 0, 0), opl3=False)
row("a corrupt 2.1 on an OPL2 is NEEDOPL3 (step 5 before 6)",
    SIG + b"\x21\x80", (NEEDOPL3, 0, 0), opl3=False)
row("over RAD_MAXLEN is BIG before NEEDOPL3",
    V2_MIN + bytes(49153 - len(V2_MIN)), (BIG, 0, 0), opl3=False)
row("17 bytes: no flags byte", SIG + b"\x21", bad(TRUNC, 0x11))
row("2.1 flags bit 7", v2(flags=0x80), bad(FLAGS, 0x11))
row("BPM word cut off", SIG + b"\x21\x20\x96", bad(TRUNC, 0x12))
row("BPM 45", v2(flags=0x20, bpm=45), bad(BPM, 0x12))
row("BPM 46", v2(flags=0x20, bpm=46), OK)
row("BPM 300", v2(flags=0x20, bpm=300), OK)
row("BPM 301", v2(flags=0x20, bpm=301), bad(BPM, 0x12))
row("BPM 0 with bit 5 clear is no BPM", v2(flags=0x40), OK)
row("description never ends", SIG + b"\x21\x00abc", bad(TRUNC, 0x12))
row("description after a BPM never ends", SIG + b"\x21\x20\x96\x00ab", bad(TRUNC, 0x14))

# instruments start at 13h
row("instrument 1, FM", v2(insts=fm(1, b"x")), OK)
row("instrument 127", v2(insts=fm(127)), OK)
row("instrument 128", v2(insts=fm(128)), bad(INST, 0x13))
row("instruments out of order", v2(insts=fm(2) + fm(2)), bad(INST, 0x13 + 26))
row("instrument list cut off", SIG + b"\x21\x00\x00", bad(TRUNC, 0x13))
row("instrument name cut off", SIG + b"\x21\x00\x00\x01\x09abc", bad(TRUNC, 0x13))
row("FM body cut off (23 of 24)", SIG + b"\x21\x00\x00" + fm(1)[:25], bad(TRUNC, 0x13))
row("MIDI instrument, 7 bytes", v2(insts=midi(1)), OK)
row("MIDI version nibble 1", v2(insts=midi(1, version=1)), bad(MIDI, 0x13))
row("MIDI body cut off (6 of 7)", SIG + b"\x21\x00\x00" + midi(1)[:8], bad(TRUNC, 0x13))
row("instrument riff", v2(insts=fm(1, riff=b"\x80\xc3\x4c")), OK)
row("instrument riff size past the file",
    SIG + b"\x21\x00\x00" + bytes((1, 0, 0x80, 0, 1, 60)) + bytes(20) + b"\x80\x00\x80\x81",
    bad(TRUNC, 0x13))
row("instrument riff with channel 9", v2(insts=fm(1, riff=b"\x80\x89")), bad(CHAN, 0x13 + 29))

# the order list: at 14h in V2_MIN
row("order list empty", v2(orders=b""), bad(ORDLEN, 0x14))
row("order list 128 long", v2(orders=bytes(128)), OK)
row("order list 129 long", v2(orders=bytes(129)), bad(ORDLEN, 0x14))
row("order list cut off", SIG + b"\x21\x00\x00\x00\x05\x00\x00", bad(TRUNC, 0x14))
row("order entry 99", v2(orders=b"\x63"), OK)
row("order entry 100", v2(orders=b"\x64"), bad(ORDER, 0x15))
row("2.1 may open with a jump marker", v2(orders=b"\x81\x00"), OK)
row("jump past the list", v2(orders=b"\x00\x82"), bad(JUMP, 0x16))
row("jump to itself is a jump to a jump", v2(orders=b"\x00\x81"), bad(JUMP, 0x16))
row("jump to a jump", v2(orders=b"\x00\x82\x81"), bad(JUMP, 0x16))

# patterns: the list at P = 16h, a track's size word at P+1, bytes from P+3
row("pattern 99", v2(pats=b"\x63" + track(b"\x80\x81")), OK)
row("pattern number 100", v2(pats=b"\x64" + track(b"\x80\x81")), bad(PATNUM, P))
row("pattern number repeats", v2(pats=PAT0 + PAT0), bad(PATNUM, P + 5))
row("pattern list never ends", SIG + b"\x21\x00\x00\x00\x01\x00", bad(TRUNC, P))
row("track size word cut off", SIG + b"\x21\x00\x00\x00\x01\x00\x00\x02", bad(TRUNC, P))
row("track size past the file", SIG + b"\x21\x00\x00\x00\x01\x00\x00\x09\x00\x80",
    bad(TRUNC, P))
row("empty track", v2pat(b""), bad(PTRUNC, P + 3))
row("line 63", v2pat(b"\xbf\x81"), OK)
row("line 64", v2pat(b"\xc0\x81"), bad(LINE, P + 3))
row("lines not ascending", v2pat(b"\x05\x81\x85\x81"), bad(LINE, P + 5))
row("line with no last-line bit, track ends", v2pat(b"\x05\x81"), bad(PTRUNC, P + 5))
row("channel 8", v2pat(b"\x80\x88"), OK)
row("channel 9", v2pat(b"\x80\x89"), bad(CHAN, P + 4))
row("channels not ascending", v2pat(b"\x80\x03\x83"), bad(CHAN, P + 5))
row("entry with no last-entry bit, track ends", v2pat(b"\x80\x01"), bad(PTRUNC, P + 5))
row("note C-3", v2pat(b"\x80\xc1\x31"), OK)
row("note key-off", v2pat(b"\x80\xc1\x0f"), OK)
row("note nibble 0", v2pat(b"\x80\xc1\x30"), bad(NOTE, P + 5))
row("note nibble 13", v2pat(b"\x80\xc1\x3d"), bad(NOTE, P + 5))
row("note nibble 14", v2pat(b"\x80\xc1\x3e"), bad(NOTE, P + 5))
row("note byte cut off", v2pat(b"\x80\xc1"), bad(PTRUNC, P + 5))
row("instrument byte 0", v2pat(b"\x80\xa1\x00"), bad(INSNUM, P + 5))
row("instrument byte 127", v2pat(b"\x80\xa1\x7f"), OK)
row("instrument byte 128", v2pat(b"\x80\xa1\x80"), bad(INSNUM, P + 5))
row("effect 31, parameter 99", v2pat(b"\x80\x91\x1f\x63"), OK)
row("effect 32", v2pat(b"\x80\x91\x20\x00"), bad(EFFECT, P + 5))
row("parameter 100", v2pat(b"\x80\x91\x01\x64"), bad(EFFECT, P + 5))
row("effect without its parameter", v2pat(b"\x80\x91\x01"), bad(PTRUNC, P + 5))
row("note, instrument and effect", v2pat(b"\x80\xf1\x31\x05\x0f\x03"), OK)
row("bytes after a track's last line", v2pat(b"\x80\x81\x00"), bad(EXTRA, P + 5))

# riffs: after the pattern list's FFh, at R = 17h with no patterns
R = 0x17
row("channel riff 0/1", v2(riffs=b"\x01" + track(b"\x80\x81")), OK)
row("channel riff 9/9", v2(riffs=b"\x99" + track(b"\x80\x81")), OK)
row("riff 10", v2(riffs=b"\xa1" + track(b"\x80\x81")), bad(RIFFID, R))
row("riff channel 0", v2(riffs=b"\x10" + track(b"\x80\x81")), bad(RIFFID, R))
row("riff channel 10", v2(riffs=b"\x1a" + track(b"\x80\x81")), bad(RIFFID, R))
row("riff id repeats", v2(riffs=(b"\x11" + track(b"\x80\x81")) * 2), bad(RIFFID, R + 5))
row("channel-riff entry without bit 7", v2(riffs=b"\x11" + track(b"\x80\x01")),
    bad(CHAN, R + 4))
row("channel riff ignores the entry's channel", v2(riffs=b"\x11" + track(b"\x80\x8f")), OK)
row("two entries on a channel-riff line", v2(riffs=b"\x11" + track(b"\x80\x81\x82")),
    bad(EXTRA, R + 5))
row("riff list never ends", V2_MIN[:-1], bad(TRUNC, R))
row("bytes after the riff list", V2_MIN + b"\x00", bad(EXTRA, 0x18))

# --- 1.0 -------------------------------------------------------------------
# The smallest valid 1.0 tune:
#   00..0F signature, 10 version 10h, 11 flags 00h, 12 instrument list's 00h,
#   13 order length 1, 14 order entry 0, 15..54 the 32-word pattern table,
#   all zero.                                                   n = 55h
T = 0x15


def v1(flags=0, desc=None, insts=b"", orders=b"\x00", table=None, tail=b""):
    out = SIG + b"\x10" + bytes((flags,))
    if desc is not None:
        out += desc + b"\0"
    out += insts + b"\0" + bytes((len(orders),)) + orders
    t = len(out)
    tab = bytearray(64)
    for k, off in (table or {}).items():
        struct.pack_into("<H", tab, 2 * k, off)
    return out + bytes(tab) + tail, t


V1_MIN, _t = v1()
assert _t == T


def v1pat(body):
    """V1_MIN with pattern 0 at 55h = `body`."""
    return v1(table={0: 0x55}, tail=body)[0]


row("1.0 minimal", V1_MIN, OK)
row("1.0 on an OPL2", V1_MIN, OK, opl3=False)
row("1.0 over RAD_MAXLEN", V1_MIN + bytes(49153 - len(V1_MIN)), (BIG, 0, 0))
row("1.0 at RAD_MAXLEN, trailing bytes are ignored", V1_MIN + bytes(49152 - len(V1_MIN)), OK)
row("1.0 flags bit 5", v1(flags=0x20)[0], bad(FLAGS, 0x11))
row("1.0 with a description", v1(flags=0x80, desc=b"hi\x01\x05there")[0], OK)
row("1.0 description never ends", SIG + b"\x10\x80abc", bad(TRUNC, 0x12))
row("1.0 flags bit 7 clear: no description read", v1(flags=0x40)[0], OK)
row("1.0 instrument 31", v1(insts=b"\x1f" + bytes(11))[0], OK)
row("1.0 instrument 32", v1(insts=b"\x20" + bytes(11))[0], bad(INST, 0x12))
row("1.0 instruments out of order", v1(insts=b"\x03" + bytes(11) + b"\x03" + bytes(11))[0],
    bad(INST, 0x12 + 12))
row("1.0 instrument cut off", SIG + b"\x10\x00\x01" + bytes(10), bad(TRUNC, 0x12))
row("1.0 order list empty", v1(orders=b"")[0], bad(ORDLEN, 0x13))
row("1.0 order entry 31", v1(orders=b"\x1f")[0], OK)
row("1.0 order entry 32", v1(orders=b"\x20")[0], bad(ORDER, 0x14))
row("1.0 order list opens with a jump", v1(orders=b"\x81\x00")[0], bad(JUMP, 0x14))
row("1.0 jump marker later", v1(orders=b"\x00\x80")[0], OK)
row("1.0 pattern table cut off", V1_MIN[:T + 63], bad(TRUNC, T))
row("1.0 pattern offset inside the header", v1(table={3: T + 63})[0], bad(PATOFF, T + 6))
row("1.0 pattern offset at the file's end", v1(table={0: 0x55})[0], bad(PATOFF, T))
row("1.0 pattern", v1pat(b"\x80\x81\x31\x10"), OK)
row("1.0 pattern with an effect", v1pat(b"\x80\x81\x31\x1c\x63"), OK)
row("1.0 line 64", v1pat(b"\xc0\x81\x31\x10"), bad(LINE, 0x55))
row("1.0 lines not ascending", v1pat(b"\x05\x81\x31\x10\x85\x81\x31\x10"), bad(LINE, 0x59))
row("1.0 no last line before the file ends", v1pat(b"\x05\x81\x31\x10"), bad(TRUNC, 0x59))
row("1.0 channel 9", v1pat(b"\x80\x89\x31\x10"), bad(CHAN, 0x56))
row("1.0 channels not ascending", v1pat(b"\x80\x04\x31\x10\x84\x31\x10"), bad(CHAN, 0x59))
row("1.0 entry cut off", v1pat(b"\x80\x81\x31"), bad(TRUNC, 0x56))
row("1.0 note 13", v1pat(b"\x80\x81\x3d\x10"), bad(NOTE, 0x57))
row("1.0 note 14", v1pat(b"\x80\x81\x3e\x10"), bad(NOTE, 0x57))
row("1.0 note nibble 0 is allowed", v1pat(b"\x80\x81\x30\x10"), OK)
row("1.0 parameter 100", v1pat(b"\x80\x81\x31\x1c\x64"), bad(EFFECT, 0x59))
row("1.0 parameter cut off", v1pat(b"\x80\x81\x31\x1c"), bad(TRUNC, 0x59))


# --- run the table ----------------------------------------------------------
eq(sorted(radsim.RADC.values()), list(range(1, 19)), "radsim's RADC_ details are 1..18",
   "SPEC.md 96.4.4's detail table")
for name, data, want, opl3 in ROWS:
    got = radsim.check(data, opl3=opl3)
    eq(got, want, "row '%s'" % name, "SPEC.md 96.4.4 - the triple verb 4 answers")
    if got is None and want is None:
        try:
            radsim.reference_stream(data, 300, opl3=opl3)
            played = True
        except IndexError as e:
            played = e
        check(played is True, "row '%s' is accepted but its replay reads outside the "
              "tune: %r" % (name, played),
              "an accepted file must never walk the replayer off its end")

# The committed fixtures are accepted, and a v2 one is refused on an OPL2.
for fname, _fn in radsim.FIXTURES:
    path = os.path.join(ROOT, "tests", "fixtures", "rad", fname)
    try:
        data = open(path, "rb").read()
    except OSError:
        check(False, "fixture %s is missing" % path, "tools/radsim.py --make writes it")
        continue
    eq(radsim.check(data), None, "fixture %s validates" % fname)
    if data[0x10] == 0x21:
        eq(radsim.check(data, opl3=False), (NEEDOPL3, 0, 0), "fixture %s on an OPL2" % fname)

# Truncation sweep: every proper prefix of every 2.1 fixture past its version
# byte is refused as CORRUPT (a 2.1 file has an end marker), and no prefix of
# any fixture makes the validator raise anything but a refusal.
for fname, fn in radsim.FIXTURES:
    data = fn()
    wrong = []
    for k in range(17, len(data)):
        try:
            got = radsim.check(data[:k])
        except Exception as e:                           # noqa: BLE001
            wrong.append((k, repr(e)))
            continue
        if data[0x10] == 0x21 and (got is None or got[0] != CORRUPT):
            wrong.append((k, got))
    check(not wrong, "prefixes of %s: %r" % (fname, wrong[:4]),
          "a cut-off 2.1 tune is always RADE_CORRUPT, and never an exception")

# Single-byte corruption sweep over RV2.RAD: whatever a flipped byte does to the
# verdict, an ACCEPTED result must still replay inside the file.
data = bytearray(radsim.FIXTURES[2][1]())
escaped = []
for i in range(0x11, len(data)):
    for mask in (0x80, 0x0F):
        d = bytearray(data)
        d[i] ^= mask
        try:
            if radsim.check(bytes(d)) is None:
                radsim.reference_stream(bytes(d), 60)
        except IndexError:
            escaped.append((i, mask))
check(not escaped, "RV2.RAD with a flipped byte replays off its end at %r" % escaped[:4],
      "SPEC.md 96.4.4 is what keeps the interrupt-time replayer inside the claim")

# The fan-out (SPEC.md 96.4.5, 2.1 deviation 5; 34.13.6). The depth guard caps
# how DEEP riffs nest, not how WIDE: three instruments, each with a speed-1
# riff whose one line plays all nine channels alternating between the other
# two, and a pattern line that plays the first on all nine. It is a VALID file
# - built here from 96.4.3, not by radsim's composer - and without the cap its
# first frame plays ~9^8 notes (the reference player too: 36.5 million writes).
def fan_line(a, b):
    body = b"\x80"
    for c in range(9):
        body += bytes(((0x80 if c == 8 else 0) | 0x60 | c, 0x31, a if c % 2 == 0 else b))
    return body


FAN = v2(insts=fm(1, riff=fan_line(2, 3)) + fm(2, riff=fan_line(3, 1)) +
         fm(3, riff=fan_line(1, 2)),
         pats=b"\x00" + track(b"\x80" + b"".join(
             bytes(((0x80 if c == 8 else 0) | 0x60 | c, 0x31, 1)) for c in range(9))))
eq(radsim.check(FAN), None, "the fan-out tune validates",
   "it is a legal file; the cap, not the validator, is what bounds it")


class _TooMuch(Exception):
    pass


def fan_frame(pnmax, write_limit):
    writes = [0]

    def wr(r, v):
        writes[0] += 1
        if writes[0] > write_limit:
            raise _TooMuch()
    eng = radsim.EngineV2(FAN, wr)
    eng.pnmax = pnmax
    writes[0] = 0
    try:
        eng.update()
    except _TooMuch:
        return None, writes[0]
    return eng.notes, writes[0]


LIMIT = radsim.RAD_PNMAX * 40           # a note play writes at most ~30 registers
eq(radsim.RAD_PNMAX, 32, "RAD_PNMAX", "SPEC.md 96.4.5 / 34.13.6")
notes, writes = fan_frame(radsim.RAD_PNMAX, LIMIT)
check(notes is not None and notes <= radsim.RAD_PNMAX,
      "the fan-out tune's frame 1 played %r notes and %d writes - past RAD_PNMAX %d"
      % (notes, writes, radsim.RAD_PNMAX),
      "SPEC.md 34.13.6: a frame's work is bounded whatever the file says")
check(notes == radsim.RAD_PNMAX,
      "the fan-out tune played %r notes, so it no longer reaches the cap it tests"
      % notes, "the row has to reach RAD_PNMAX to test it")
# negative control: the same frame with the cap taken out runs past any bound
uncapped, uwrites = fan_frame(1 << 30, LIMIT * 20)
check(uncapped is None,
      "with the cap taken out the fan-out frame stopped at %r notes / %d writes - the "
      "row cannot tell a capped replayer from an uncapped one" % (uncapped, uwrites),
      "negative control")

# ...and the frame that reaches the cap is the tune's LAST (deviation 5): the
# reference stream is the start, that one frame and the stop sequence.
_st, _frames, _sp = radsim.frames_of(radsim.reference_stream(FAN, 200))
eq(len(_frames), 1, "frames the fan-out tune plays before it halts",
   "SPEC.md 96.4.5 deviation 5: a refused note play halts the tune")

# SUSTAINED work (SPEC.md 34.13.3 step 5, 34.13.5, 34.13.6): the per-frame cap
# alone bounds one frame, and RAD_FRMAX frames an interrupt - with the RTC's
# credit re-arming it - multiplied that into a machine held at IF = 0. The
# bound is per BIOS TICK: at most 2 x RAD_PNMAX - 1 note plays and
# WORK_TICK computed reference writes, on both pacer classes, whatever the
# file. Two valid tunes at BPM 300 and speed 1, one line on every frame:
#   FANALL - every line plays the fan-out instrument on all nine channels,
#            so every frame would reach the cap (it must halt, on tick 0)
#   HEAVY  - every line plays it on three, 30 note plays a frame, UNDER the
#            cap: it must play on, and the per-tick budget is what bounds it
WORK_TICK = 2560                        # SPEC.md 34.13.6's figure


def _lines(ents):
    body = b""
    for n in range(64):
        body += bytes(((0x80 if n == 63 else 0) | n,)) + ents
    return body


def _on(chans):
    return b"".join(bytes(((0x80 if c == chans - 1 else 0) | 0x60 | c, 0x31, 1))
                    for c in range(chans))


FANALL = v2(flags=0x21, bpm=300,
            insts=fm(1, riff=fan_line(2, 3)) + fm(2, riff=fan_line(3, 1)) +
            fm(3, riff=fan_line(1, 2)),
            pats=b"\x00" + track(_lines(_on(9))))
HEAVY = v2(flags=0x21, bpm=300,
           insts=fm(1, alg=6, riff=fan_line(2, 3)) + fm(2, alg=2) + fm(3, alg=4),
           pats=b"\x00" + track(_lines(_on(3))))
for name, tune, halts in (("FANALL", FANALL, True), ("HEAVY", HEAVY, False)):
    eq(radsim.check(tune), None, "%s validates" % name, "a legal file")
    eq(radsim.rate_tenths(tune), 1200, "%s's rate" % name, "the rate ceiling")
    for rtc in (False, True):
        cls = "RTC" if rtc else "tick"
        per, halted = radsim.paced(tune, 64, rtc=rtc)
        worst_n = max(p[1] for p in per)
        worst_w = max(p[2] for p in per)
        check(worst_n <= 2 * radsim.RAD_PNMAX - 1,
              "%s on the %s class: %d note plays in one tick" % (name, cls, worst_n),
              "SPEC.md 34.13.6: 2 x RAD_PNMAX - 1 a tick")
        check(worst_w <= WORK_TICK,
              "%s on the %s class: %d computed writes in one tick" % (name, cls, worst_w),
              "SPEC.md 34.13.6: WORK_TICK a tick")
        if halts:
            eq(halted, 0, "%s halts on the %s class at tick" % (name, cls),
               "deviation 5: the first capped frame ends the tune")
        else:
            eq(halted, None, "%s halts on the %s class at tick" % (name, cls),
               "under the cap a heavy tune plays on, slowly")
            check(len(per) == 64 and min(p[0] for p in per) >= 1,
                  "%s on the %s class stopped playing" % (name, cls),
                  "the budget delays frames, it never starves the tune")
        # negative control: round 0's rule - 81 a FRAME, no tick budget, no halt
        per0, _h = radsim.paced(tune, 64, rtc=rtc, budget=False, halt=False, pnmax=81)
        check(max(p[2] for p in per0) > WORK_TICK and
              max(p[1] for p in per0) > 2 * radsim.RAD_PNMAX - 1,
              "%s on the %s class under the per-frame rule stays within the bound "
              "(%d writes) - the row cannot tell the rules apart"
              % (name, cls, max(p[2] for p in per0)), "negative control")

done("t_rad")
