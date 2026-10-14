#!/usr/bin/env python3
"""os88dia - the host's own reader and writer for a DIA's two stores
(SPEC.md 107).

A Desktop Integrated Application keeps two things in two places, each where
the OS already keeps that kind of state:

  * WHICH widgets are open and where each was launched from - one DIA ROW per
    widget in SYSTEM.CFG's desktop trailer (SPEC.md 26.8.6, 107.2): a 128-byte
    shortcut record with KIND 1 and 'D' at +3;
  * its WINDOW'S PLACE and its data - SYSTEM\\APPDATA\\<NAME>.DAT (SPEC.md
    19.9, 107.5): a 16-byte DIA header, then the application's payload. The
    Desk Calendar's payload v1 is coded here too (DESKCAL_*).

This file is an INDEPENDENT SECOND READER, written from SPEC.md and not from
the kernel's source: every fixture `make diatest` boots is written by it, and
every assertion a gate makes about what the machine wrote back is read by it.
It mirrors the kernel's boot reader (ovl_sc_load) in what it REFUSES, so a
fixture it accepts is one the machine accepts.

    os88dia.py cfg -o SYSTEM.CFG [--want BITS] [--dia A:SYSTEM/DIA/X.O88=Cap]...
                                 [--short A:PATH=Caption@kind,cell]...
    os88dia.py dat -o X.DAT --x 303 --y 184 --screen 640x480 [--payver 1]
                   [--paylen 0 | --deskcal]
    os88dia.py dump FILE         a SYSTEM.CFG or a .DAT, as JSON
    os88dia.py --selfcheck       round-trip and refusal checks; exit 0 = good
"""

import argparse
import json
import struct
import sys

# --- SPEC.md 26.8.1 / 26.8.6 / 107.2 -----------------------------------------
SC_REC = 128
SC_VER = 2
SC_MAX = 15
SC_R_VOL = 0
SC_R_SLOT = 1
SC_R_KIND = 2
SC_R_DIA = 3
SC_R_PATH = 4
SC_PATHMAX = 45
SC_R_CAP = 49
SC_CAPMAX = 13
SC_R_HDR = 62
SC_R_BODY = 64
DIA_MARK = 0x44                 # 'D'
DESK_SC0 = 9                    # the pins block: zones 0..8
DVOL_MAX = 8
DSL_PIN, DSL_GONE, DSL_SLOT = 0x40, 0x80, 0x3F
CFG_SIG = b"O88CFG\0\0"
CFG_GEN_V = 3
CFG_REC0 = 10
CFG_FBUF = 141                  # SPEC.md 2.8.7: a file this size or less
                                # carries no trailer the boot will read.
                                # kern_big's - the only kernel that reads a
                                # trailer - and DERIVED there (driver.inc), so
                                # tests/unit/t_dia.py evaluates the kernel's
                                # own expression with nasm and holds this to
                                # it: it was 131 until #248's two pin keys
CFG_MAXFILE = 4096              # ...and one this size or more is refused

# --- SPEC.md 107.5 ------------------------------------------------------------
DIA_HDR_SIZE = 16
DIA_HDR_MAGIC = b"DA"
DIA_HDR_VER = 1
DIA_HF_POS = 1

# --- the Desk Calendar's payload v1 (SPEC.md 107.3, 107.5) --------------------
DESKCAL_PAYVER = 1
DESKCAL_NAPPT, DESKCAL_NTASK = 48, 32
DESKCAL_APPT = 32               # +0 date, +2 time, +4 flags, +5 lead, +6 26 text
DESKCAL_TASK = 28               # +0 flags, +1 0, +2 due, +4 24 text
DESKCAL_APPT0 = 8
DESKCAL_TASK0 = DESKCAL_APPT0 + DESKCAL_NAPPT * DESKCAL_APPT     # 1544
DESKCAL_PAYLEN = DESKCAL_TASK0 + DESKCAL_NTASK * DESKCAL_TASK    # 2440
APPT_REMIND, APPT_FIRED = 1, 2
TASK_DONE, TASK_DATED = 1, 2
TIME_ALLDAY = 0xFFFF

# --- where a RUNNING package keeps things, as offsets past its image ---------
# The gates read these out of a live guest (tests/deskcalgrid.py, diaboot.py
# and their kin); tests/unit/t_dia.py has NASM evaluate the package's own
# equs and holds these equal, so a moved field fails `make` and not a soak.
DESKCAL_DIA_BSS = 272           # deskcal.asm's DC_BSS: its own state first
DESKCAL_BSS_GRID = 0            # 42 bytes, the month on show
DESKCAL_BSS_Y = 42              # dw the year on show
DESKCAL_BSS_M = 44              # db its month
DESKCAL_BSS_SEL = 45            # db the selected day
DESKCAL_BSS_TY = 46             # dw today's year
DESKCAL_BSS_TM = 48             # db ...month
DESKCAL_BSS_TD = 49             # db ...day
DESKCAL_BSS_OX = 62             # dw the content origin, as the last draw
DESKCAL_BSS_OY = 64             # found it
DESKCAL_BSS_ED = 99             # db the entry field: 0 shut, 1..4 its mode
DESKCAL_BSS_EDI = 100           # db ...the item it edits
DESKCAL_BSS_SELK = 101          # db the selected item: 0, 1 appt, 2 task
DESKCAL_BSS_SELI = 102          # db ...its index in its table
DESKCAL_BSS_EBUF = 134          # 32: the entry field's text, NUL
DESKCAL_BSS_TTOP = 105          # db the first task on show
DESKCAL_BSS_TSHOW = 107         # db task rows on the glass, to the pager
DESKCAL_BSS_ALERT = 211         # db a reminder's alert is up (wave 4)
DESKCAL_BSS_NOW = 212           # dw the minute of the day, as the last beat
DESKCAL_BSS_LNOW = 214          # dw ...and the one the reminders looked at
DESKCAL_BSS_RMSG = 224          # 42: `Reminder HH:MM text`, the toast at +9
DESKCAL_NOREM = 1               # payload +3 bit 0: new ones get no reminder


def dia_st_off(dia_bss, paylen):
    """os88dia.inc's dia_st, as an offset past the image: the DIA header,
    the payload, the 128-byte row, the 13-byte name, the banked folder word
    and its drive byte."""
    return dia_bss + DIA_HDR_SIZE + paylen + SC_REC + 13 + 2 + 1


def dia_hdr_off(dia_bss):
    """The DIA header in the package's bss (os88dia.inc's dia_file)."""
    return dia_bss


class DiaError(ValueError):
    pass


def _cstr(b):
    return bytes(b).split(b"\0")[0].decode("latin-1")


def _path(p):
    """`A:SYSTEM/DIA/X.O88` -> (0, 'SYSTEM\\DIA\\X.O88')."""
    vol = 0
    if len(p) >= 2 and p[1] == ":":
        vol = ord(p[0].upper()) - ord("A")
        p = p[2:]
    return vol, p.replace("/", "\\").lstrip("\\").upper()


# =============================================================================
# the row and the trailer
# =============================================================================

def row(vol, path, cap, kind=1, mark=0, slot=0xFF, body=None):
    """One 128-byte record. `mark` is +3's byte: DIA_MARK for a DIA row,
    0x5C ('\\') for what a drag-out writes, 0 for a package's link."""
    p = path.encode("latin-1")
    c = cap.encode("latin-1")
    if len(p) > SC_PATHMAX - 1:
        raise DiaError("path %r is over %d characters" % (path, SC_PATHMAX - 1))
    if len(c) > SC_CAPMAX - 1:
        raise DiaError("caption %r is over %d characters" % (cap, SC_CAPMAX - 1))
    r = bytearray(SC_REC)
    r[SC_R_VOL], r[SC_R_SLOT], r[SC_R_KIND], r[SC_R_DIA] = vol, slot, kind, mark
    r[SC_R_PATH:SC_R_PATH + len(p)] = p
    r[SC_R_CAP:SC_R_CAP + len(c)] = c
    r[SC_R_HDR:SC_R_HDR + 2] = b"\x01\x10"
    if body is None:            # a framed square: something to see
        body = (b"\xff\xff" * 16 + b"\xff\xff" + b"\x01\x80" * 14
                + b"\xff\xff")
    if len(body) != 64:
        raise DiaError("an icon body is 64 bytes")
    r[SC_R_BODY:] = body
    return bytes(r)


def dia_row(vol, path, cap, body=None):
    """A DIA row as CTRL.DRV keeps it: KIND 1, the mark, no cell."""
    return row(vol, path, cap, kind=1, mark=DIA_MARK, slot=0xFF, body=body)


def settings_head(records=b""):
    """The settings part of SYSTEM.CFG: signature, generation, records, and
    the `dw 0` terminator. `records` is raw key/ver/len/data bytes."""
    return CFG_SIG + struct.pack("<H", CFG_GEN_V) + records + b"\0\0"


def cfg(rows=(), pins=None, head=None):
    """A whole SYSTEM.CFG: the settings head, then the trailer when there is
    anything to keep (SPEC.md 26.8.6)."""
    head = settings_head() if head is None else head
    rows = list(rows)
    if len(rows) > SC_MAX:
        raise DiaError("%d rows: the table holds %d" % (len(rows), SC_MAX))
    pins = bytes([0xFF] * DESK_SC0) if pins is None else bytes(pins)
    if len(pins) != DESK_SC0:
        raise DiaError("the pins block is %d bytes" % DESK_SC0)
    if not rows and all(b == 0xFF for b in pins):
        return head
    out = head + b"".join(rows) + pins + b"SC" + bytes([SC_VER, len(rows)])
    if len(out) >= CFG_MAXFILE:
        raise DiaError("%d bytes: the boot reader refuses %d or more"
                       % (len(out), CFG_MAXFILE))
    return out


def parse_row(r):
    r = bytes(r)
    return {
        "vol": r[SC_R_VOL], "slot": r[SC_R_SLOT], "kind": r[SC_R_KIND],
        "mark": r[SC_R_DIA], "dia": r[SC_R_DIA] == DIA_MARK,
        "path": _cstr(r[SC_R_PATH:SC_R_PATH + SC_PATHMAX - 1]),
        "cap": _cstr(r[SC_R_CAP:SC_R_CAP + SC_CAPMAX - 1]),
        "hdr": list(r[SC_R_HDR:SC_R_HDR + 2]),
    }


def parse_cfg(data):
    """SYSTEM.CFG as the boot reader sees it. Answers a dict: `head` (bytes),
    `rows` (a list of parse_row, FREE rows - the kernel's refusals - kept as
    None), `pins` and `trailer` (False when there is none the boot reads).
    Raises DiaError only for a file the settings reader itself refuses."""
    data = bytes(data)
    if len(data) < CFG_REC0 or data[:8] != CFG_SIG:
        raise DiaError("not a SYSTEM.CFG: no O88CFG signature")
    if struct.unpack_from("<H", data, 8)[0] != CFG_GEN_V:
        raise DiaError("SYSTEM.CFG generation %d, not %d"
                       % (struct.unpack_from("<H", data, 8)[0], CFG_GEN_V))
    none = {"head": data, "rows": [], "pins": None, "trailer": False}
    if len(data) <= CFG_FBUF or len(data) >= CFG_MAXFILE:
        return none
    end = len(data) - 4 - DESK_SC0
    if end < 0 or data[-4:-2] != b"SC" or data[-2] != SC_VER:
        return none
    n = data[-1]
    if n > SC_MAX:
        return none
    start = end - n * SC_REC
    if start < CFG_REC0 + 2:
        return none
    rows = []
    for i in range(n):
        r = data[start + i * SC_REC:start + (i + 1) * SC_REC]
        # ovl_sc_load: a kind above 2 or a volume past DVOL_MAX is a FREE row
        if r[SC_R_KIND] > 2 or r[SC_R_VOL] >= DVOL_MAX:
            rows.append(None)
        else:
            rows.append(parse_row(r))
    return {"head": data[:start], "rows": rows,
            "pins": list(data[end:end + DESK_SC0]), "trailer": True}


def dia_rows(data):
    """Just the DIA rows a SYSTEM.CFG carries, in file order."""
    return [r for r in parse_cfg(data)["rows"]
            if r is not None and r["dia"] and r["kind"] == 1]


# =============================================================================
# the state file
# =============================================================================

def dat(payload=b"", x=None, y=None, screen=None, payver=1):
    """A DIA state file: header + payload. A position needs all of x, y and
    the screen it was saved on."""
    payload = bytes(payload)
    pos = x is not None
    if pos and (y is None or screen is None):
        raise DiaError("a position is x, y AND the screen it was saved on")
    sw, sh = screen if pos else (0, 0)
    hdr = DIA_HDR_MAGIC + struct.pack(
        "<BBHHHHBBH", DIA_HDR_VER, DIA_HF_POS if pos else 0,
        x if pos else 0, y if pos else 0, sw, sh, payver, 0, len(payload))
    assert len(hdr) == DIA_HDR_SIZE
    return hdr + payload


def parse_dat(data, paylen, payver):
    """The package's own test (os88dia.inc's dia_load): the EXACT size, the
    magic, the header version, the payload's version and length. Raises
    DiaError naming the first refusal - a file that is not exactly this is
    DAMAGED to the package, never partly read."""
    data = bytes(data)
    if len(data) != DIA_HDR_SIZE + paylen:
        raise DiaError("%d bytes, not %d" % (len(data), DIA_HDR_SIZE + paylen))
    if data[:2] != DIA_HDR_MAGIC:
        raise DiaError("no 'DA' magic")
    ver, flags, x, y, sw, sh, pv, z, ln = struct.unpack_from(
        "<BBHHHHBBH", data, 2)
    if ver != DIA_HDR_VER:
        raise DiaError("header version %d" % ver)
    if pv != payver:
        raise DiaError("payload version %d, not %d" % (pv, payver))
    if ln != paylen:
        raise DiaError("payload length %d, not %d" % (ln, paylen))
    out = {"pos": bool(flags & DIA_HF_POS), "x": x, "y": y,
           "screen": [sw, sh], "payver": pv, "paylen": ln,
           "payload": data[DIA_HDR_SIZE:]}
    return out


# --- the Desk Calendar's payload ----------------------------------------------

def pack_date(y, m, d):
    """FAT-packed: a word compare orders it."""
    if not (1980 <= y <= 2099 and 1 <= m <= 12 and 1 <= d <= 31):
        raise DiaError("date %d-%d-%d out of range" % (y, m, d))
    return ((y - 1980) << 9) | (m << 5) | d


def unpack_date(w):
    return (w >> 9) + 1980, (w >> 5) & 15, w & 31


def deskcal(appts=(), tasks=(), lead=5, flags=0):
    """appts: (date-tuple, minute-of-day or None, text, remind, fired, lead),
    remind defaulting to "timed" - an all-day one has none (SPEC.md 107.3);
    tasks: (text, done, due-date-tuple or None). Sorted by (date, time + 1) as the
    package keeps them."""
    appts, tasks = list(appts), list(tasks)
    if len(appts) > DESKCAL_NAPPT or len(tasks) > DESKCAL_NTASK:
        raise DiaError("too many appointments or tasks")
    out = bytearray(DESKCAL_PAYLEN)
    out[0:4] = bytes([len(appts), len(tasks), lead, flags])
    recs = []
    for a in appts:
        date, t, text = a[0], a[1], a[2]
        remind = a[3] if len(a) > 3 else (t is not None)  # all day: none
        fired = a[4] if len(a) > 4 else False
        al = a[5] if len(a) > 5 else lead
        recs.append((pack_date(*date), TIME_ALLDAY if t is None else t,
                     (APPT_REMIND if remind else 0)
                     | (APPT_FIRED if fired else 0), al, text))
    # the package's own order (SPEC.md 107.3): (date, time + 1) as words,
    # so ALL DAY (0FFFFh) wraps to 0 and comes first in its day
    recs.sort(key=lambda r: (r[0], (r[1] + 1) & 0xFFFF))
    for i, (d, t, f, al, text) in enumerate(recs):
        b = text.encode("latin-1")[:25]
        o = DESKCAL_APPT0 + i * DESKCAL_APPT
        struct.pack_into("<HHBB", out, o, d, t, f, al)
        out[o + 6:o + 6 + len(b)] = b
    for i, tk in enumerate(tasks):
        text, done = tk[0], tk[1]
        due = tk[2] if len(tk) > 2 else None
        b = text.encode("latin-1")[:23]
        o = DESKCAL_TASK0 + i * DESKCAL_TASK
        out[o] = (TASK_DONE if done else 0) | (TASK_DATED if due else 0)
        struct.pack_into("<H", out, o + 2, pack_date(*due) if due else 0)
        out[o + 4:o + 4 + len(b)] = b
    return bytes(out)


def parse_deskcal(p):
    p = bytes(p)
    if len(p) != DESKCAL_PAYLEN:
        raise DiaError("a DESKCAL payload is %d bytes" % DESKCAL_PAYLEN)
    na, nt, lead, flags = p[0], p[1], p[2], p[3]
    if na > DESKCAL_NAPPT or nt > DESKCAL_NTASK:
        raise DiaError("counts %d/%d past the table" % (na, nt))
    appts, tasks = [], []
    for i in range(na):
        o = DESKCAL_APPT0 + i * DESKCAL_APPT
        d, t, f, al = struct.unpack_from("<HHBB", p, o)
        appts.append({"date": list(unpack_date(d)),
                      "time": None if t == TIME_ALLDAY else t,
                      "remind": bool(f & APPT_REMIND),
                      "fired": bool(f & APPT_FIRED), "lead": al,
                      "text": _cstr(p[o + 6:o + 32])})
    for i in range(nt):
        o = DESKCAL_TASK0 + i * DESKCAL_TASK
        f = p[o]
        due = struct.unpack_from("<H", p, o + 2)[0]
        tasks.append({"done": bool(f & TASK_DONE),
                      "due": list(unpack_date(due)) if f & TASK_DATED
                      else None, "text": _cstr(p[o + 4:o + 28])})
    return {"lead": lead, "flags": flags, "appts": appts, "tasks": tasks}


# =============================================================================
# --selfcheck
# =============================================================================

def selfcheck():
    fails = []

    def ok(cond, what):
        if not cond:
            fails.append(what)

    def refuses(fn, what):
        try:
            fn()
        except DiaError:
            return
        fails.append("did not refuse: " + what)

    # the row, field by field
    r = dia_row(0, "SYSTEM\\DIA\\DESKCAL.O88", "Calendar")
    ok(len(r) == SC_REC, "a row is 128 bytes")
    ok(r[2] == 1 and r[3] == 0x44 and r[1] == 0xFF, "KIND 1, 'D', no cell")
    ok(r[4:4 + 22] == b"SYSTEM\\DIA\\DESKCAL.O88" and r[4 + 22] == 0,
       "the path below the root, 22 of 45")
    refuses(lambda: row(0, "X" * 45, "c"), "a 45-character path")
    refuses(lambda: row(0, "X", "C" * 13), "a 13-character caption")

    # the trailer, round trip
    s = row(0, "SYSTEM\\TASKMGR.O88", "TASKMGR", kind=1, mark=0x5C,
            slot=DSL_PIN | 3)
    f = cfg([s, r])
    p = parse_cfg(f)
    ok(p["trailer"] and len(p["rows"]) == 2, "two rows back")
    ok(p["rows"][0]["mark"] == 0x5C and not p["rows"][0]["dia"],
       "a drag-made '\\' at +3 is NOT a DIA row")
    ok(p["rows"][1]["dia"] and p["rows"][1]["path"]
       == "SYSTEM\\DIA\\DESKCAL.O88", "the DIA row back")
    ok(p["head"] == settings_head(), "the settings head untouched")
    ok(p["pins"] == [0xFF] * DESK_SC0, "nine pins, none placed")
    ok(len(dia_rows(f)) == 1, "dia_rows finds one")
    ok(cfg([]) == settings_head(), "nothing to keep, no trailer")

    # what the boot reader refuses, mirrored
    bad = bytearray(f)
    bad[-1] = SC_MAX + 1
    ok(not parse_cfg(bytes(bad))["trailer"], "a count past SC_MAX: no trailer")
    bad = bytearray(f)
    bad[-2] = 1
    ok(not parse_cfg(bytes(bad))["trailer"], "SC_VER 1 is refused")
    bad = bytearray(f)
    bad[-4] = ord("X")
    ok(not parse_cfg(bytes(bad))["trailer"], "no 'SC' footer: no trailer")
    k = bytearray(r)
    k[2] = 3
    ok(parse_cfg(cfg([bytes(k)]))["rows"] == [None],
       "a kind above 2 comes in a FREE row")
    k = bytearray(r)
    k[0] = DVOL_MAX
    ok(parse_cfg(cfg([bytes(k)]))["rows"] == [None],
       "a volume past DVOL_MAX comes in a FREE row")
    refuses(lambda: cfg([r] * 16), "sixteen rows")
    refuses(lambda: parse_cfg(b"NOTACFG!\3\0\0\0"), "a foreign file")
    ok(not parse_cfg(settings_head() + b"\0" * 20)["trailer"],
       "a file of CFG_FBUF or less carries no trailer")

    # the DIA header and the probe's empty payload
    d = dat(b"", x=303, y=184, screen=(640, 480))
    ok(len(d) == 16 and d[:2] == b"DA", "an empty payload is 16 bytes")
    q = parse_dat(d, 0, 1)
    ok(q["pos"] and (q["x"], q["y"], q["screen"]) == (303, 184, [640, 480]),
       "the place back")
    refuses(lambda: parse_dat(d + b"\0", 0, 1), "one byte too long")
    refuses(lambda: parse_dat(d[:-1], 0, 1), "one byte short")
    refuses(lambda: parse_dat(b"XA" + d[2:], 0, 1), "the wrong magic")
    refuses(lambda: parse_dat(d, 0, 2), "another payload version")
    refuses(lambda: dat(b"", x=1), "a position without its screen")
    ok(not parse_dat(dat(b"\0" * 4), 4, 1)["pos"], "no position is no POS bit")

    # the Desk Calendar's payload
    pay = deskcal(appts=[((2026, 10, 13), 570, "Dentist"),
                         ((2026, 10, 13), None, "Birthday"),
                         ((2026, 10, 1), 600, "Earlier", False)],
                  tasks=[("Buy milk", False), ("Call", True, (2026, 10, 20))])
    ok(len(pay) == DESKCAL_PAYLEN == 2440, "the payload is 2,440 bytes")
    full = dat(pay, x=400, y=40, screen=(640, 480), payver=DESKCAL_PAYVER)
    ok(len(full) == 2456, "the whole file is 2,456 bytes, five sectors")
    dc = parse_deskcal(parse_dat(full, DESKCAL_PAYLEN, 1)["payload"])
    ok([a["text"] for a in dc["appts"]] == ["Earlier", "Birthday", "Dentist"],
       "kept sorted by (date, time + 1): all-day FIRST in its day")
    ok(dc["appts"][2]["time"] == 570 and dc["appts"][2]["remind"],
       "9:30 with its reminder")
    ok(dc["appts"][1]["time"] is None, "all-day reads back as all-day")
    ok(not dc["appts"][1]["remind"],
       "an all-day appointment has no reminder by default, as the package")
    ok(dc["tasks"][1]["done"] and dc["tasks"][1]["due"] == [2026, 10, 20],
       "a done, dated task")
    ok(unpack_date(pack_date(2099, 12, 31)) == (2099, 12, 31)
       and pack_date(1980, 1, 1) == 0x21, "the date's two ends")
    refuses(lambda: pack_date(2100, 1, 1), "the year 2100")
    bad = bytearray(pay)
    bad[0] = 49
    refuses(lambda: parse_deskcal(bytes(bad)), "49 appointments")

    for f_ in fails:
        print("os88dia: FAIL  %s" % f_)
    print("os88dia: selfcheck %s (%d failure(s))"
          % ("ok" if not fails else "FAILED", len(fails)))
    return 1 if fails else 0


# =============================================================================
# the command line
# =============================================================================

def _jsonable(o):
    if isinstance(o, bytes):
        return o.hex()
    raise TypeError(type(o))


def main(argv):
    if "--selfcheck" in argv:
        return selfcheck()
    ap = argparse.ArgumentParser(prog="os88dia.py", description=__doc__,
                                 formatter_class=argparse.RawTextHelpFormatter)
    sub = ap.add_subparsers(dest="cmd")
    c = sub.add_parser("cfg", help="write a SYSTEM.CFG with a desktop trailer")
    c.add_argument("-o", required=True)
    c.add_argument("--want", type=lambda v: int(v, 0), default=None,
                   help="the drivers wanted, SPEC.md 51.3's 'DW' bitmap - "
                        "HDD.DRV is bit 1")
    c.add_argument("--dia", action="append", default=[],
                   help="VOL:PATH=Caption - a DIA row")
    c.add_argument("--short", action="append", default=[],
                   help="VOL:PATH=Caption@kind,cell - a drag-made shortcut "
                        "('\\' at +3), placed in that cell")
    d = sub.add_parser("dat", help="write a DIA state file")
    d.add_argument("-o", required=True)
    d.add_argument("--x", type=int)
    d.add_argument("--y", type=int)
    d.add_argument("--screen", help="WxH it was saved on")
    d.add_argument("--payver", type=int, default=1)
    d.add_argument("--paylen", type=int, default=0)
    d.add_argument("--deskcal", action="store_true",
                   help="an EMPTY Desk Calendar payload (v1, 2,440 bytes)")
    u = sub.add_parser("dump", help="a SYSTEM.CFG or a .DAT, as JSON")
    u.add_argument("file")
    u.add_argument("--paylen", type=int)
    u.add_argument("--payver", type=int, default=1)
    a = ap.parse_args(argv)

    if a.cmd == "cfg":
        rows = []
        for spec in a.dia:
            where, cap = spec.split("=", 1)
            vol, path = _path(where)
            rows.append(dia_row(vol, path, cap))
        for spec in a.short:
            where, rest = spec.split("=", 1)
            cap, how = rest.rsplit("@", 1)
            kind, cell = (int(v, 0) for v in how.split(","))
            vol, path = _path(where)
            rows.append(row(vol, path, cap, kind=kind, mark=0x5C,
                            slot=DSL_PIN | cell))
        rec = b""
        if a.want is not None:
            rec = b"DW" + bytes([1, 2]) + struct.pack("<H", a.want)
        data = cfg(rows, head=settings_head(rec))
        open(a.o, "wb").write(data)
        print("os88dia: %s %d bytes, %d row(s), %d DIA"
              % (a.o, len(data), len(rows), len(a.dia)))
        return 0
    if a.cmd == "dat":
        if a.deskcal:
            payload, payver = deskcal(), DESKCAL_PAYVER
        else:
            payload, payver = bytes(a.paylen), a.payver
        screen = tuple(int(v) for v in a.screen.split("x")) if a.screen \
            else None
        data = dat(payload, x=a.x, y=a.y, screen=screen, payver=payver)
        open(a.o, "wb").write(data)
        print("os88dia: %s %d bytes" % (a.o, len(data)))
        return 0
    if a.cmd == "dump":
        data = open(a.file, "rb").read()
        if data[:8] == CFG_SIG:
            out = parse_cfg(data)
            out["head"] = len(out["head"])
        else:
            n = a.paylen if a.paylen is not None else len(data) - DIA_HDR_SIZE
            out = parse_dat(data, n, a.payver)
            if n == DESKCAL_PAYLEN:
                out["deskcal"] = parse_deskcal(out["payload"])
            out["payload"] = len(out["payload"])
        print(json.dumps(out, indent=1, default=_jsonable))
        return 0
    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
