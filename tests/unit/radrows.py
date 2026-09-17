#!/usr/bin/env python3
"""The RAD hostile-file table, as data (SPEC.md 34.12.1, 96.4.4).

Each row is a file built HERE, by hand, from SPEC.md 96.4 - not by radsim's
fixture composer - and the `(RADE_*, RADC_*, offset)` triple SPEC.md says it
earns. Three readers hold three implementations to it:

  tests/unit/t_rad.py   radsim's validator, on the host, in `make`
  tests/radopl3.py      SOUND.DRV + RADPLAY.DRV's validator on MartyPC's OPL3
  tests/radopl2.py      ...and on its OPL2 (MARTYPC_OPL2=1)

The emulator rows ask radsim for the answer on the machine's own chip
(`radsim.check(data, opl3=...)`), so every row runs on both machines whatever
its `opl3` column says; t_rad.py asserts the column's answer.
"""
import struct

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


# --- the fan-out tunes (SPEC.md 96.4.5 deviation 5, 34.13.6) -----------------
# t_rad.py holds radsim to them; RADGATE's disks carry FAN.RAD, FANALL.RAD and
# HEAVY.RAD (tests/radgate/mkrows.py) so the driver's HALT and its per-tick
# budget run on the machine too.
def fan_line(a, b):
    body = b"\x80"
    for c in range(9):
        body += bytes(((0x80 if c == 8 else 0) | 0x60 | c, 0x31, a if c % 2 == 0 else b))
    return body


FAN = v2(insts=fm(1, riff=fan_line(2, 3)) + fm(2, riff=fan_line(3, 1)) +
         fm(3, riff=fan_line(1, 2)),
         pats=b"\x00" + track(b"\x80" + b"".join(
             bytes(((0x80 if c == 8 else 0) | 0x60 | c, 0x31, 1)) for c in range(9))))
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
