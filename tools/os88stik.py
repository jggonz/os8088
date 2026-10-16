#!/usr/bin/env python3
"""os88stik - the host's own reader and writer for a Sticky App's two stores
(SPEC.md 110).

A Sticky App keeps two things in two places, each where
the OS already keeps that kind of state:

  * WHICH widgets are open and where each was launched from - one Sticky App ROW per
    widget in SYSTEM.CFG's desktop trailer (SPEC.md 26.8.6, 110.2): a 128-byte
    shortcut record with KIND 1 and 'D' at +3;
  * its WINDOW'S PLACE and its data - SYSTEM\\APPDATA\\<NAME>.DAT (SPEC.md
    19.9, 110.5): a 16-byte Sticky App header, then the application's payload. The
    Desk Calendar's payload v1 is coded here too (DESKCAL_*).

This file is an INDEPENDENT SECOND READER, written from SPEC.md and not from
the kernel's source: every fixture `make stiktest` boots is written by it, and
every assertion a gate makes about what the machine wrote back is read by it.
It mirrors the kernel's boot reader (ovl_sc_load) in what it REFUSES, so a
fixture it accepts is one the machine accepts.

    os88stik.py cfg -o SYSTEM.CFG [--want BITS] [--stik A:STIKAPPS/X.O88=Cap]...
                                 [--short A:PATH=Caption@kind,cell]...
    os88stik.py dat -o X.DAT --x 303 --y 184 --screen 640x480 [--payver 1]
                   [--paylen 0 | --deskcal] [--cont]
                 --cont: x,y are a widget's FACE origin (header +3 bit 1,
                 STIK_HF_CONT, SPEC.md 110.11), not a titled frame's
    os88stik.py dump FILE         a SYSTEM.CFG or a .DAT, as JSON
    os88stik.py --selfcheck       round-trip and refusal checks; exit 0 = good
"""

import argparse
import json
import struct
import sys

# --- SPEC.md 26.8.1 / 26.8.6 / 110.2 -----------------------------------------
SC_REC = 128
SC_VER = 2
SC_MAX = 15
SC_R_VOL = 0
SC_R_SLOT = 1
SC_R_KIND = 2
SC_R_STIK = 3
SC_R_PATH = 4
SC_PATHMAX = 45
SC_R_CAP = 49
SC_CAPMAX = 13
SC_R_HDR = 62
SC_R_BODY = 64
STIK_MARK = 0x44                 # 'D'
DESK_SC0 = 9                    # the pins block: zones 0..8
DVOL_MAX = 8
DSL_PIN, DSL_GONE, DSL_SLOT = 0x40, 0x80, 0x3F
CFG_SIG = b"O88CFG\0\0"
CFG_GEN_V = 3
CFG_REC0 = 10
CFG_FBUF = 163                  # SPEC.md 2.8.7: a file this size or less
                                # carries no trailer the boot will read.
                                # kern_big's - the only kernel that reads a
                                # trailer - and DERIVED there (driver.inc), so
                                # tests/unit/t_stik.py evaluates the kernel's
                                # own expression with nasm and holds this to
                                # it: it was 131 until #248's two pin keys,
                                # and 152 until #249's 'TP' theme preset
CFG_MAXFILE = 4096              # ...and one this size or more is refused

# --- SPEC.md 110.5 ------------------------------------------------------------
STIK_HDR_SIZE = 16
STIK_HDR_MAGIC = b"DA"
STIK_HDR_VER = 1
STIK_HF_POS = 1
STIK_HF_CONT = 2                 # v2 (SPEC.md 110.11): +4/+6 are the FACE's
TITLE_H = 18                    # ...and a v1 file's frame is a title bar up

# --- the Desk Calendar's payload v2 (SPEC.md 110.3, 110.5): 16 and 16, where
# v1 was 48 and 32 - one cluster on a 360KB disk instead of three -----------
DESKCAL_PAYVER = 2
DESKCAL_NAPPT, DESKCAL_NTASK = 16, 16
DESKCAL_APPT = 32               # +0 date, +2 time, +4 flags, +5 lead, +6 26 text
DESKCAL_TASK = 28               # +0 flags, +1 0, +2 due, +4 24 text
DESKCAL_APPT0 = 8
DESKCAL_TASK0 = DESKCAL_APPT0 + DESKCAL_NAPPT * DESKCAL_APPT     # 520
DESKCAL_PAYLEN = DESKCAL_TASK0 + DESKCAL_NTASK * DESKCAL_TASK    # 968
DESKCAL_SNZT = 4                # v2: dw the snooze slot's minute of the day
DESKCAL_SNZD = 6                # ...and its FAT date (read only beside a
                                # SNOOZED appointment, so v1's zeros mean
                                # nothing - SPEC.md 110.3)
APPT_REMIND, APPT_FIRED = 1, 2
APPT_SNOOZED, APPT_DUE = 4, 8   # v2: snoozed; fired and not dismissed (card
                                # 3 rings it). A v1 file has neither
TASK_DONE, TASK_DATED = 1, 2
TIME_ALLDAY = 0xFFFF
DESKCAL_SNOOZE = 5              # deskcal's DC_SNOOZE, minutes

# --- where a RUNNING package keeps things, as offsets past its image ---------
# The gates read these out of a live guest (tests/deskcalgrid.py, stikboot.py
# and their kin); tests/unit/t_stik.py has NASM evaluate the package's own
# equs and holds these equal, so a moved field fails `make` and not a soak.
DESKCAL_STIK_BSS = 1024          # deskcal.asm's DC_BSS: its own state first
DESKCAL_BSS_GRID = 0            # 42 bytes, the month on show (Monday first)
DESKCAL_BSS_Y = 42              # dw the year on show
DESKCAL_BSS_M = 44              # db its month
DESKCAL_BSS_SEL = 45            # db the selected day
DESKCAL_BSS_TY = 46             # dw today's year
DESKCAL_BSS_TM = 48             # db ...month
DESKCAL_BSS_TD = 49             # db ...day
DESKCAL_BSS_ED = 62             # db the entry field: 0 shut, 1..5 its mode
DESKCAL_BSS_EDI = 63            # db ...the item it edits
DESKCAL_BSS_SELK = 64           # db the selected item: 0, 1 appt, 2 task
DESKCAL_BSS_SELI = 65           # db ...its index in its table
DESKCAL_BSS_PANE = 66           # db COMPACT's slot: 0 day, 1 tasks, 2 reminder
DESKCAL_BSS_ATOP = 68           # db the agenda's first item on show
DESKCAL_BSS_TTOP = 69           # db the first task on show
DESKCAL_BSS_ACNT = 74           # db the selected day's appointments
DESKCAL_BSS_NOW = 80            # dw the minute of the day, as the last beat
DESKCAL_BSS_LNOW = 82           # dw ...and the one the reminders looked at
DESKCAL_BSS_C3K = 86            # db card 3: 0 none, 1 next, 2 RINGING
DESKCAL_BSS_C3A = 87            # db ...its appointment
DESKCAL_BSS_C3T = 88            # dw ...the time it shows
DESKCAL_BSS_LIT = 90            # db a HELD button's card + 1, 0 none
DESKCAL_BSS_EBUF = 180          # 32: the entry field's text, NUL
DESKCAL_BSS_RMSG = 244          # 42: the last toast's line, NUL
DESKCAL_BSS_LINE = 288          # 32: dc_line, the string the last compose built
DESKCAL_BSS_BAND = 320          # 704: the composer's band, 176 x 32
DESKCAL_NOREM = 1               # payload +3 bit 0: new ones get no reminder
DESKCAL_C3 = {0: "none", 1: "next", 2: "ringing"}


# The calendar's three layouts (deskcal's dc_lays and dc_geos, SPEC.md
# 110.3), by [stik_li]: 0 TALL (VGA), 1 MID (Hercules, EGA), 2 COMPACT (CGA).
# Rows are CARD-LOCAL, x face-relative. t_stik reads dc_geos' bytes out of the
# assembled package and holds DESKCAL_GEO_FIELDS to them, in this order
DESKCAL_GEO_FIELDS = ("BIGS", "BIGY", "DOWX", "DOWY", "MONY", "NAVY", "NAVL",
                      "NAVR", "NAVM", "HDRB", "WKY", "W0", "WP", "DOTY",
                      "DOTH", "BOXT", "BOXH", "HY", "A0", "AP", "AN", "RULE",
                      "T0", "TP", "TN", "TX", "TTY", "BOX", "BAR", "BARH",
                      "ATN", "TTN", "BELLY", "RX", "R1Y", "R2X", "R2Y", "R2N",
                      "LKY", "LKX", "NONEX")     # then dw BELL, dw BOXR, BELLX
DESKCAL_DOTBY = 0x80            # DOTY's flag: the dot BESIDE the number, 2 x 1
                                # one pixel past its cell, and the box 22 wide
                                # round both (dcdraw.inc's DCG_DOTBY)


def deskcal_box(G, field_x):
    """(x, width) of a day's box - today's inverse one, the selection's
    outline - in a field at `field_x`: dcdraw.inc's dc_cday1."""
    return field_x + 2, 22 if G["DOTY"] & DESKCAL_DOTBY else 20


def deskcal_dot(G, field_x, day):
    """(x, rows below the number's row, w, h) of day `day`'s dot in a field
    at `field_x`: dcdraw.inc's dc_cday1."""
    if G["DOTY"] & DESKCAL_DOTBY:
        return (field_x + (16 if day < 10 else 20), G["DOTY"] & 0x7F, 2,
                G["DOTH"])
    return field_x + 11, G["DOTY"], 2, G["DOTH"]


DESKCAL_GEO = {
    0: dict(zip(DESKCAL_GEO_FIELDS, (
        0x0A, 7, 64, 9, 22, 44, 8, 160, 0, 40, 60, 78, 16, 9, 2, 4, 16,
        6, 22, 14, 2, 50, 57, 16, 2, 32, 2, 11, 2, 12, 14, 17,
        5, 32, 8, 32, 25, 17, 41, 32, 28)), BELLX=8,
        cards=((178, 6), (91, 6), (57, 5)), tag=12, face=(176, 355),
        fy=42),
    1: dict(zip(DESKCAL_GEO_FIELDS, (
        0x06, 4, 48, 4, 13, 26, 8, 160, 0, 22, 38, 50, 12, 8, 1, 2, 12,
        4, 16, 11, 2, 39, 42, 12, 2, 24, 1, 9, 1, 10, 14, 18,
        3, 22, 4, 68, 4, 13, 18, 22, 26)), BELLX=4,
        cards=((122, 4), (67, 4), (30, 4)), tag=11, face=(176, 242),
        fy=24),
    2: dict(zip(DESKCAL_GEO_FIELDS, (
        0x06, 2, 48, 2, 0xFF, 10, 48, 136, 64, 19, 20, 30, 9,
        DESKCAL_DOTBY | 3, 1, 1, 9,
        3, 14, 10, 3, 0, 14, 10, 3, 24, 0, 8, 1, 10, 14, 18,
        14, 24, 14, 24, 24, 18, 34, 24, 24)), BELLX=8,
        cards=((85, 3), (44, 3)), tag=11, face=(176, 146), fy=22),
}
DESKCAL_RIGHT = 264             # the default face x: screen width - 264
DESKCAL_FACE_W = 176


def deskcal_card_top(li, card):
    """Card `card`'s first row, face-relative, in layout `li`."""
    return sum(h + g for h, g in DESKCAL_GEO[li]["cards"][:card])


def stik_st_off(stik_bss, paylen):
    """os88stik.inc's stik_st, as an offset past the image: the Sticky App header,
    the payload, the 128-byte row, the 13-byte name, the banked folder word
    and its drive byte."""
    return stik_bss + STIK_HDR_SIZE + paylen + SC_REC + 13 + 2 + 1


def stik_hdr_off(stik_bss):
    """The Sticky App header in the package's bss (os88stik.inc's stik_file)."""
    return stik_bss


# os88stik.inc's STIK_WIDGET fields (SPEC.md 110.11), after v1's stik_nx - in
# the include's order; tests/unit/t_stik.py has NASM evaluate each against a
# STIK_WIDGET build and holds this table to it
WIDGET_FIELDS = (("stik_wf", 1), ("stik_hv", 1), ("stik_tagp", 1),
                 ("stik_press", 1), ("stik_li", 1), ("stik_ci", 1),
                 ("stik_lay", 2), ("stik_fh", 2), ("stik_fx", 2), ("stik_fy", 2),
                 ("stik_cy", 2), ("stik_ch", 2), ("stik_dmg", 8), ("stik_pp", 2),
                 ("stik_gh", 2), ("stik_tbeat", 2), ("stik_thov", 2),
                 ("stik_qx", 2), ("stik_qy", 2), ("stik_pre", 1), ("stik_sc", 1),
                 ("stik_ss", 2), ("stik_sr0", 2), ("stik_sr1", 2), ("stik_ty", 2))
STIK_HV = {0: "hidden", 1: "dwell", 2: "shown", 3: "linger"}
STIK_W_DESK, STIK_W_FOCUS, STIK_W_ARMED = 1, 2, 4
STIK_P = {0: "rest", 1: "focus", 2: "handle", 3: "armed", 0xFF: "none"}


def widget_off(stik_bss, paylen):
    """{field: offset past the image} for a STIK_WIDGET package's state -
    stik_st, then v1's 37 bytes (stik_st .. stik_nx), then WIDGET_FIELDS."""
    off = stik_st_off(stik_bss, paylen) + 1 + 2 + 2 + 2 + 1 + 1 + 24 + 1 + 2 + 1
    out = {}
    for name, size in WIDGET_FIELDS:
        out[name] = off
        off += size
    out["end"] = off
    return out


class DiaError(ValueError):
    pass


def _cstr(b):
    return bytes(b).split(b"\0")[0].decode("latin-1")


def _path(p):
    """`A:STIKAPPS/X.O88` -> (0, 'STIKAPPS\\X.O88')."""
    vol = 0
    if len(p) >= 2 and p[1] == ":":
        vol = ord(p[0].upper()) - ord("A")
        p = p[2:]
    return vol, p.replace("/", "\\").lstrip("\\").upper()


# =============================================================================
# the row and the trailer
# =============================================================================

def row(vol, path, cap, kind=1, mark=0, slot=0xFF, body=None):
    """One 128-byte record. `mark` is +3's byte: STIK_MARK for a Sticky App row,
    0x5C ('\\') for what a drag-out writes, 0 for a package's link."""
    p = path.encode("latin-1")
    c = cap.encode("latin-1")
    if len(p) > SC_PATHMAX - 1:
        raise DiaError("path %r is over %d characters" % (path, SC_PATHMAX - 1))
    if len(c) > SC_CAPMAX - 1:
        raise DiaError("caption %r is over %d characters" % (cap, SC_CAPMAX - 1))
    r = bytearray(SC_REC)
    r[SC_R_VOL], r[SC_R_SLOT], r[SC_R_KIND], r[SC_R_STIK] = vol, slot, kind, mark
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


def stik_row(vol, path, cap, body=None):
    """A Sticky App row as CTRL.DRV keeps it: KIND 1, the mark, no cell."""
    return row(vol, path, cap, kind=1, mark=STIK_MARK, slot=0xFF, body=body)


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
    tail = b"".join(rows) + pins + b"SC" + bytes([SC_VER, len(rows)])
    # A file of CFG_FBUF or less is never read for a trailer (ovl_sc_load's
    # first compare). The kernel's own writer cannot make one - it rewrites
    # EVERY key, so its head alone is CFG_FBUF (SPEC.md 26.8.6) - but a
    # fixture's head is only the keys the test asked for, and one row behind
    # the bare head stopped clearing it when #249's 'TP' grew CFG_FBUF to
    # 163. So pad AFTER the terminator, which both readers stop at: the
    # settings reader at the `dw 0`, the trailer reader from the END.
    head += bytes(max(0, CFG_FBUF + 1 - len(head) - len(tail)))
    out = head + tail
    if len(out) >= CFG_MAXFILE:
        raise DiaError("%d bytes: the boot reader refuses %d or more"
                       % (len(out), CFG_MAXFILE))
    return out


def parse_row(r):
    r = bytes(r)
    return {
        "vol": r[SC_R_VOL], "slot": r[SC_R_SLOT], "kind": r[SC_R_KIND],
        "mark": r[SC_R_STIK], "stik": r[SC_R_STIK] == STIK_MARK,
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


def stik_rows(data):
    """Just the Sticky App rows a SYSTEM.CFG carries, in file order."""
    return [r for r in parse_cfg(data)["rows"]
            if r is not None and r["stik"] and r["kind"] == 1]


# =============================================================================
# the state file
# =============================================================================

def dat(payload=b"", x=None, y=None, screen=None, payver=None, cont=False):
    """A Sticky App state file: header + payload. A position needs all of x, y and
    the screen it was saved on; `cont` says they are a widget's FACE origin
    (STIK_HF_CONT), which only a STIK_WIDGET package writes.

    `payver` None is the payload's OWN version: the Desk Calendar's
    (DESKCAL_PAYVER) for a payload of the calendar's length, and 1 - the
    probes' - for anything else. A fixed 1 here wrote every calendar fixture
    as v1 the day the calendar's payload went to v2, and the calendar called
    each one damaged."""
    payload = bytes(payload)
    if payver is None:
        payver = DESKCAL_PAYVER if len(payload) == DESKCAL_PAYLEN else 1
    pos = x is not None
    if pos and (y is None or screen is None):
        raise DiaError("a position is x, y AND the screen it was saved on")
    sw, sh = screen if pos else (0, 0)
    hdr = STIK_HDR_MAGIC + struct.pack(
        "<BBHHHHBBH", STIK_HDR_VER,
        (STIK_HF_POS if pos else 0) | (STIK_HF_CONT if cont else 0),
        x if pos else 0, y if pos else 0, sw, sh, payver, 0, len(payload))
    assert len(hdr) == STIK_HDR_SIZE
    return hdr + payload


def parse_dat(data, paylen, payver):
    """The package's own test (os88stik.inc's stik_load): the EXACT size, the
    magic, the header version, the payload's version and length. Raises
    DiaError naming the first refusal - a file that is not exactly this is
    DAMAGED to the package, never partly read."""
    data = bytes(data)
    # AS LONG AS ITS CONTENT (SPEC.md 110.5.3): a save writes the payload
    # less its zero tail, so 0..paylen bytes follow the header and +14 says
    # how many; the reader gives the tail back as zeros
    if not STIK_HDR_SIZE <= len(data) <= STIK_HDR_SIZE + paylen:
        raise DiaError("%d bytes, not %d..%d" % (len(data), STIK_HDR_SIZE,
                                                 STIK_HDR_SIZE + paylen))
    if data[:2] != STIK_HDR_MAGIC:
        raise DiaError("no 'DA' magic")
    ver, flags, x, y, sw, sh, pv, z, ln = struct.unpack_from(
        "<BBHHHHBBH", data, 2)
    if ver != STIK_HDR_VER:
        raise DiaError("header version %d" % ver)
    if pv != payver:
        raise DiaError("payload version %d, not %d" % (pv, payver))
    if ln != len(data) - STIK_HDR_SIZE:
        raise DiaError("payload length %d, not the file's %d"
                       % (ln, len(data) - STIK_HDR_SIZE))
    cont = bool(flags & STIK_HF_CONT)
    out = {"pos": bool(flags & STIK_HF_POS), "x": x, "y": y, "cont": cont,
           # the FACE a widget reads it as (SPEC.md 110.11): its own origin,
           # or a v1 file's titled frame - a border in, a title bar down
           "face": [x, y] if cont else [x + 1, y + TITLE_H],
           "screen": [sw, sh], "payver": pv, "paylen": ln,
           "payload": data[STIK_HDR_SIZE:] + bytes(paylen - ln)}
    return out


def trim_payload(payload):
    """What os88stik.inc's stik_save writes of a payload: all of it but its
    zero tail (SPEC.md 110.5.3)."""
    return bytes(payload).rstrip(b"\0")


# --- the Desk Calendar's payload ----------------------------------------------

def pack_date(y, m, d):
    """FAT-packed: a word compare orders it."""
    if not (1980 <= y <= 2099 and 1 <= m <= 12 and 1 <= d <= 31):
        raise DiaError("date %d-%d-%d out of range" % (y, m, d))
    return ((y - 1980) << 9) | (m << 5) | d


def unpack_date(w):
    return (w >> 9) + 1980, (w >> 5) & 15, w & 31


def deskcal(appts=(), tasks=(), lead=5, flags=0, snooze=None):
    """appts: (date-tuple, minute-of-day or None, text, remind, fired, lead,
    due, snoozed), remind defaulting to "timed" - an all-day one has none
    (SPEC.md 110.3) - and due / snoozed to False (v2, SPEC.md 110.3); tasks:
    (text, done, due-date-tuple or None); snooze: (date-tuple, minute), the
    snooze slot at payload +4/+6 (v2), or None for v1's zeros. Sorted by
    (date, time + 1) as the package keeps them."""
    appts, tasks = list(appts), list(tasks)
    if len(appts) > DESKCAL_NAPPT or len(tasks) > DESKCAL_NTASK:
        raise DiaError("too many appointments or tasks")
    out = bytearray(DESKCAL_PAYLEN)
    out[0:4] = bytes([len(appts), len(tasks), lead, flags])
    if snooze is not None:
        struct.pack_into("<HH", out, DESKCAL_SNZT, snooze[1],
                         pack_date(*snooze[0]))
    recs = []
    for a in appts:
        date, t, text = a[0], a[1], a[2]
        remind = a[3] if len(a) > 3 else (t is not None)  # all day: none
        fired = a[4] if len(a) > 4 else False
        al = a[5] if len(a) > 5 else lead
        due = a[6] if len(a) > 6 else False
        snz = a[7] if len(a) > 7 else False
        recs.append((pack_date(*date), TIME_ALLDAY if t is None else t,
                     (APPT_REMIND if remind else 0)
                     | (APPT_FIRED if fired else 0)
                     | (APPT_DUE if due else 0)
                     | (APPT_SNOOZED if snz else 0), al, text))
    # the package's own order (SPEC.md 110.3): (date, time + 1) as words,
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
    st, sd = struct.unpack_from("<HH", p, DESKCAL_SNZT)
    appts, tasks = [], []
    for i in range(na):
        o = DESKCAL_APPT0 + i * DESKCAL_APPT
        d, t, f, al = struct.unpack_from("<HHBB", p, o)
        appts.append({"date": list(unpack_date(d)),
                      "time": None if t == TIME_ALLDAY else t,
                      "remind": bool(f & APPT_REMIND),
                      "fired": bool(f & APPT_FIRED),
                      "due": bool(f & APPT_DUE),
                      "snoozed": bool(f & APPT_SNOOZED), "lead": al,
                      "text": _cstr(p[o + 6:o + 32])})
    for i in range(nt):
        o = DESKCAL_TASK0 + i * DESKCAL_TASK
        f = p[o]
        due = struct.unpack_from("<H", p, o + 2)[0]
        tasks.append({"done": bool(f & TASK_DONE),
                      "due": list(unpack_date(due)) if f & TASK_DATED
                      else None, "text": _cstr(p[o + 4:o + 28])})
    snooze = None
    if any(a["snoozed"] for a in appts):
        snooze = {"date": list(unpack_date(sd)), "time": st}
    return {"lead": lead, "flags": flags, "appts": appts, "tasks": tasks,
            "snooze": snooze}


# --- Sticky Note's payload (SPEC.md 110.13) ------------------------------------
# stiknote.asm's SNP_* / SN_*; tests/unit/t_stik.py holds every one of these to
# the package's own build, evaluated by NASM
STIKNOTE_PAYVER = 1
STIKNOTE_MAXN = 24               # notes...
STIKNOTE_POOL = 4096             # ...sharing one pool of text
STIKNOTE_N = 0                   # db notes in use
STIKNOTE_CUR = 1                 # db the current one: the pad's page and the
                                 # editor's selection
STIKNOTE_FLAGS = 2               # db STIKNOTE_F_*
STIKNOTE_F_DESK = 1              # the pad is on the desktop
STIKNOTE_F_INIT = 0x80           # the welcome note was made once
STIKNOTE_COL = 4                 # 24 db: each note's paper
STIKNOTE_LENS = 28               # 24 dw: each note's length, in pool order
STIKNOTE_POOL0 = 80              # the text, note after note
STIKNOTE_PAYLEN = STIKNOTE_POOL0 + STIKNOTE_POOL    # 4176
STIKNOTE_BOX0, STIKNOTE_BOX1 = 1, 2   # a body line's first byte: a box
STIKNOTE_PAPERS = ("yellow", "pink", "green", "blue", "white")
STIKNOTE_STIK_BSS = 2064         # stiknote.asm's SN_BSS: its own state first
STIKNOTE_BSS_WIN = 0             # dw the editor's window, 0 never made
STIKNOTE_BSS_OPEN = 4            # db the editor is up
STIKNOTE_BSS_CAR = 8             # dw the caret, an offset into the note
STIKNOTE_BSS_TOP = 10            # dw the editor's first body line on show
STIKNOTE_FACE_W = 192            # 24 cells: the sheet and the stack behind it
STIKNOTE_RIGHT = 472             # the default face x: screen width - 472
STIKNOTE_S = 4                   # the stack's offset, both ways
STIKNOTE_TW, STIKNOTE_BTW = 21, 19    # the pad's cells: a text line, a box line
# the pad's geometry for each layout (snpad.inc's snp_geos), card-local rows
STIKNOTE_GEO_FIELDS = ("H", "BW", "HY", "PINY", "RY", "RH", "B0", "P", "NV",
                       "FY", "OY", "K", "BOXY", "LOFF")
STIKNOTE_GEO = {
    0: dict(H=156, BW=2, HY=8, PINY=6, RY=21, RH=2, B0=29, P=12, NV=8,
            FY=134, OY=133, K=12, BOXY=0xFF, LOFF=2),
    1: dict(H=124, BW=1, HY=6, PINY=6, RY=17, RH=1, B0=23, P=10, NV=7,
            FY=104, OY=102, K=10, BOXY=0xFF, LOFF=1),
    2: dict(H=92, BW=1, HY=4, PINY=4, RY=14, RH=1, B0=18, P=9, NV=6,
            FY=76, OY=76, K=8, BOXY=0, LOFF=1),
}
# ...where on the card a press lands (snpad.inc's SNP_*X)
STIKNOTE_PLUSX, STIKNOTE_PREVX, STIKNOTE_NEXTX, STIKNOTE_OPENX = 170, 56, 128, 150
STIKNOTE_BX, STIKNOTE_BTX = 10, 26
# the editor's (sned.inc's sne_geos): content-local rows, in its order
STIKNOTE_ED_FIELDS = ("TBH", "BY", "BH", "BTY", "BBW", "CBY", "LHY", "L0",
                      "LP", "ICY", "TY", "RY", "RH", "B0", "P", "LOFF", "BOXY",
                      "SBH", "AH", "AX", "AY", "HMIN")
STIKNOTE_ED = {
    0: dict(TBH=28, BY=6, BH=16, BTY=4, BBW=2, CBY=9, LHY=6, L0=22, LP=15,
            ICY=0xFE, TY=6, RY=17, RH=2, B0=25, P=12, LOFF=2, BOXY=0xFF,
            SBH=16, AH=16, AX=2, AY=2, HMIN=110),
    1: dict(TBH=24, BY=4, BH=15, BTY=4, BBW=1, CBY=7, LHY=4, L0=17, LP=12,
            ICY=0xFF, TY=4, RY=14, RH=1, B0=20, P=10, LOFF=1, BOXY=0xFF,
            SBH=14, AH=12, AX=3, AY=2, HMIN=96),
    2: dict(TBH=20, BY=3, BH=13, BTY=3, BBW=1, CBY=6, LHY=3, L0=14, LP=10,
            ICY=0xFF, TY=3, RY=13, RH=1, B0=17, P=9, LOFF=1, BOXY=0,
            SBH=12, AH=11, AX=3, AY=2, HMIN=84),
}
STIKNOTE_ED_LW = 144             # the list's width: the page starts here
STIKNOTE_ED_NEW = (8, 64)        # the bar's buttons: x, width
STIKNOTE_ED_DEL = (80, 72)
STIKNOTE_ED_CBX = 168            # ...and the On desktop box


def stiknote(notes=(), cur=0, desk=True, init=True):
    """A Sticky Note payload: `notes` is a list of (text, paper) - the text a
    str whose first line is the title and whose body lines may begin with
    STIKNOTE_BOX0 / BOX1 (a box, open or ticked), the paper an index into
    STIKNOTE_PAPERS."""
    notes = list(notes)
    if len(notes) > STIKNOTE_MAXN:
        raise DiaError("%d notes, at most %d" % (len(notes), STIKNOTE_MAXN))
    p = bytearray(STIKNOTE_PAYLEN)
    p[STIKNOTE_N] = len(notes)
    p[STIKNOTE_CUR] = cur if notes else 0
    p[STIKNOTE_FLAGS] = (STIKNOTE_F_DESK if desk else 0) | \
        (STIKNOTE_F_INIT if init else 0)
    at = STIKNOTE_POOL0
    for k, (text, paper) in enumerate(notes):
        b = text.encode("latin-1")
        if at + len(b) > STIKNOTE_PAYLEN:
            raise DiaError("the notes are more than %d bytes" % STIKNOTE_POOL)
        if not 0 <= paper < len(STIKNOTE_PAPERS):
            raise DiaError("paper %d" % paper)
        p[STIKNOTE_COL + k] = paper
        struct.pack_into("<H", p, STIKNOTE_LENS + 2 * k, len(b))
        p[at:at + len(b)] = b
        at += len(b)
    return bytes(p)


def parse_stiknote(p):
    """The payload back, with stiknote.asm's own refusals (sn_check): the
    count, the current note, the papers, the lengths and their sum."""
    p = bytes(p)
    if len(p) != STIKNOTE_PAYLEN:
        raise DiaError("%d bytes, not %d" % (len(p), STIKNOTE_PAYLEN))
    n, cur, fl = p[STIKNOTE_N], p[STIKNOTE_CUR], p[STIKNOTE_FLAGS]
    if n > STIKNOTE_MAXN:
        raise DiaError("%d notes" % n)
    if (n and cur >= n) or (not n and cur):
        raise DiaError("current note %d of %d" % (cur, n))
    lens = struct.unpack_from("<%dH" % STIKNOTE_MAXN, p, STIKNOTE_LENS)
    if any(lens[n:]):
        raise DiaError("a length past the count")
    if sum(lens) > STIKNOTE_POOL:
        raise DiaError("%d bytes of text" % sum(lens))
    notes, at = [], STIKNOTE_POOL0
    for k in range(n):
        if p[STIKNOTE_COL + k] >= len(STIKNOTE_PAPERS):
            raise DiaError("note %d's paper" % k)
        text = p[at:at + lens[k]].decode("latin-1")
        at += lens[k]
        title, _, body = text.partition("\n")
        notes.append({"text": text, "title": title,
                      "paper": STIKNOTE_PAPERS[p[STIKNOTE_COL + k]],
                      "body": stiknote_paras(body) if "\n" in text else []})
    return {"n": n, "cur": cur, "desk": bool(fl & STIKNOTE_F_DESK),
            "init": bool(fl & STIKNOTE_F_INIT), "notes": notes}


def stiknote_paras(body):
    """A note's body as paragraphs: (kind, text) with kind None for text,
    False for an open box and True for a ticked one."""
    out = []
    for line in body.split("\n"):
        if line[:1] == chr(STIKNOTE_BOX0):
            out.append((False, line[1:]))
        elif line[:1] == chr(STIKNOTE_BOX1):
            out.append((True, line[1:]))
        else:
            out.append((None, line))
    return out


# --- the Desk Clock's payload (SPEC.md 110.14.5) --------------------------------
# deskclok.asm's DKP_* / DKF_* / DK_*; tests/unit/t_stik.py holds every one of
# these to the package's own numbers, evaluated by NASM
DESKCLOK_PAYLEN = 32
DESKCLOK_PAYVER = 1
DESKCLOK_FACE = 0                # db the face, 0..8
DESKCLOK_FLAGS = 1               # db DESKCLOK_F_*
DESKCLOK_F_ONLY = 1              # `Clock face only`
DESKCLOK_F_SECS = 2              # seconds shown
DESKCLOK_F_H24 = 4               # 24-hour
DESKCLOK_F_ALARM = 8             # the alarm is set
DESKCLOK_F_INIT = 0x80           # the defaults were made once
DESKCLOK_SIZE = 2                # db 0 Small, 1 Medium, 2 Large
DESKCLOK_AH = 3                  # db the alarm's hour, 0..23
DESKCLOK_AM = 4                  # db ...its minute
DESKCLOK_LABEL = 5               # 17: the label, 0..16 characters and a NUL
DESKCLOK_LABMAX = 16
DESKCLOK_FACES = ("Classic", "Memphis '80", "Neon '90", "Mono Digital",
                  "Casio LCD", "Casio VGA", "Red LED", "Amber LED",
                  "Flip Clock")
DESKCLOK_ANALOG = 3              # faces 0..2 have hands
DESKCLOK_SIZES = ("Small", "Medium", "Large")
DESKCLOK_SZW = ((80, 112, 152), (96, 128, 176))   # a face's width: dial, digital
DESKCLOK_STIK_BSS = 5088         # deskclok.asm's DK_BSS: its own state first
DESKCLOK_BSS_WIN = 251           # dw the clock's window
DESKCLOK_BSS_CW = 271            # dw the card's width...
DESKCLOK_BSS_CH = 273            # dw ...and height
DESKCLOK_BSS_RING = 382          # db 0 quiet, 1 ringing, 2 rung out
DESKCLOK_BSS_SNZ = 383           # db snoozed
DESKCLOK_BSS_SWIN = 388          # dw the settings window, 0 none
DESKCLOK_BSS_AWIN = 437          # dw the alarm window, 0 none
DESKCLOK_BSS_COK = 136           # db the face cache holds the face


def deskclok(face=0, only=False, secs=False, h24=True, alarm=False, size=1,
             ah=7, am=0, label="Wake up", init=True):
    """A Desk Clock payload: what dk_check accepts, the face an index into
    DESKCLOK_FACES and the size into DESKCLOK_SIZES."""
    if not 0 <= face < len(DESKCLOK_FACES):
        raise DiaError("face %d" % face)
    if not 0 <= size < len(DESKCLOK_SIZES):
        raise DiaError("size %d" % size)
    if not (0 <= ah < 24 and 0 <= am < 60):
        raise DiaError("alarm %d:%d" % (ah, am))
    lab = label.encode("latin-1")
    if len(lab) > DESKCLOK_LABMAX or any(c < 32 or c > 126 for c in lab):
        raise DiaError("label %r" % label)
    p = bytearray(DESKCLOK_PAYLEN)
    p[DESKCLOK_FACE] = face
    p[DESKCLOK_FLAGS] = ((DESKCLOK_F_ONLY if only else 0) |
                         (DESKCLOK_F_SECS if secs else 0) |
                         (DESKCLOK_F_H24 if h24 else 0) |
                         (DESKCLOK_F_ALARM if alarm else 0) |
                         (DESKCLOK_F_INIT if init else 0))
    p[DESKCLOK_SIZE] = size
    p[DESKCLOK_AH] = ah
    p[DESKCLOK_AM] = am
    p[DESKCLOK_LABEL:DESKCLOK_LABEL + len(lab)] = lab
    return bytes(p)


def parse_deskclok(p):
    """The payload back, with deskclok.asm's own refusals (dk_check): a
    payload it would replace with the defaults raises DiaError."""
    p = bytes(p)
    if len(p) != DESKCLOK_PAYLEN:
        raise DiaError("%d bytes, not %d" % (len(p), DESKCLOK_PAYLEN))
    fl = p[DESKCLOK_FLAGS]
    if not fl & DESKCLOK_F_INIT:
        raise DiaError("no INIT mark: the defaults")
    if p[DESKCLOK_FACE] >= len(DESKCLOK_FACES) or p[DESKCLOK_SIZE] >= 3:
        raise DiaError("face %d, size %d" % (p[DESKCLOK_FACE], p[DESKCLOK_SIZE]))
    if p[DESKCLOK_AH] >= 24 or p[DESKCLOK_AM] >= 60:
        raise DiaError("alarm %d:%d" % (p[DESKCLOK_AH], p[DESKCLOK_AM]))
    lab = p[DESKCLOK_LABEL:DESKCLOK_LABEL + DESKCLOK_LABMAX + 1]
    if 0 not in lab:
        raise DiaError("the label has no NUL")
    lab = lab[:lab.index(0)]
    if any(c < 32 or c > 126 for c in lab):
        raise DiaError("the label is not printable")
    return {"face": p[DESKCLOK_FACE],
            "face_name": DESKCLOK_FACES[p[DESKCLOK_FACE]],
            "only": bool(fl & DESKCLOK_F_ONLY),
            "secs": bool(fl & DESKCLOK_F_SECS),
            "h24": bool(fl & DESKCLOK_F_H24),
            "alarm": bool(fl & DESKCLOK_F_ALARM),
            "size": p[DESKCLOK_SIZE],
            "ah": p[DESKCLOK_AH], "am": p[DESKCLOK_AM],
            "label": lab.decode("latin-1")}


def stiknote_wrap(body, w=STIKNOTE_TW, wb=STIKNOTE_BTW):
    """sntext.inc's WALK, mirrored: the body cut into visual lines - a
    paragraph that does not fit breaks after its last space (the space is
    not drawn), or hard at the width when it has none. Each line is (kind,
    first, text): `kind` as stiknote_paras', `first` True on a paragraph's
    first line, which is the one a box is drawn on."""
    out = []
    for kind, text in stiknote_paras(body):
        width = w if kind is None else wb
        first, s = True, text
        while True:
            if len(s) <= width:
                out.append((kind, first, s))
                break
            if s[width] == " ":
                cut, nxt = width, width + 1
            else:
                sp = s.rfind(" ", 0, width)
                cut, nxt = (sp, sp + 1) if sp > 0 else (max(width, 1),
                                                        max(width, 1))
                if sp == 0:
                    cut, nxt = 0, 1
            out.append((kind, first, s[:cut]))
            first, s = False, s[nxt:]
    return out


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
    r = stik_row(0, "STIKAPPS\\DESKCAL.O88", "Calendar")
    ok(len(r) == SC_REC, "a row is 128 bytes")
    ok(r[2] == 1 and r[3] == 0x44 and r[1] == 0xFF, "KIND 1, 'D', no cell")
    ok(r[4:4 + 20] == b"STIKAPPS\\DESKCAL.O88" and r[4 + 20] == 0,
       "the path below the root, 20 of 45")
    refuses(lambda: row(0, "X" * 45, "c"), "a 45-character path")
    refuses(lambda: row(0, "X", "C" * 13), "a 13-character caption")

    # the trailer, round trip
    s = row(0, "SYSTEM\\TASKMGR.O88", "TASKMGR", kind=1, mark=0x5C,
            slot=DSL_PIN | 3)
    f = cfg([s, r])
    p = parse_cfg(f)
    ok(p["trailer"] and len(p["rows"]) == 2, "two rows back")
    ok(p["rows"][0]["mark"] == 0x5C and not p["rows"][0]["stik"],
       "a drag-made '\\' at +3 is NOT a Sticky App row")
    ok(p["rows"][1]["stik"] and p["rows"][1]["path"]
       == "STIKAPPS\\DESKCAL.O88", "the Sticky App row back")
    ok(p["head"][:len(settings_head())] == settings_head(),
       "the settings head untouched")
    ok(p["pins"] == [0xFF] * DESK_SC0, "nine pins, none placed")
    ok(len(stik_rows(f)) == 1, "stik_rows finds one")
    ok(cfg([]) == settings_head(), "nothing to keep, no trailer")
    ok(len(cfg([r])) > CFG_FBUF and parse_cfg(cfg([r]))["trailer"],
       "one row behind the bare head still clears CFG_FBUF")

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

    # the Sticky App header and the probe's empty payload
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
    ok(not q["cont"] and q["face"] == [304, 202],
       "a v1 file's titled frame is read as a face a border in, a bar down")
    c = parse_dat(dat(b"", x=376, y=184, screen=(640, 480), cont=True), 0, 1)
    ok(c["cont"] and c["pos"] and c["face"] == [376, 184] and
       dat(b"", x=1, y=2, screen=(3, 4), cont=True)[3] == 3,
       "a face origin round-trips with +3 = POS | CONT")
    # AS LONG AS ITS CONTENT (SPEC.md 110.5.3): the zero tail is not written
    t = trim_payload(b"ab\0c\0\0\0")
    ok(t == b"ab\0c", "the trim keeps an inner zero and drops the tail")
    sh = parse_dat(dat(t), 7, 1)
    ok(sh["paylen"] == 4 and sh["payload"] == b"ab\0c\0\0\0",
       "a short file reads back zero-padded to the payload's length")
    ok(parse_dat(dat(b""), 7, 1)["payload"] == bytes(7),
       "an all-zero payload is a header alone")
    refuses(lambda: parse_dat(dat(b"x" * 8), 7, 1), "a payload too long")
    refuses(lambda: parse_dat(dat(b"xy")[:-1], 7, 1),
            "a file shorter than +14 says")
    w = widget_off(288, 0)
    ok(w["stik_wf"] == stik_st_off(288, 0) + 37 and w["stik_hv"] == w["stik_wf"] + 1,
       "the widget's fields follow v1's 37 bytes")

    # the Desk Clock's payload
    dk = deskclok(face=6, only=True, secs=True, alarm=True, ah=21, am=0,
                  label="Back up files")
    ok(len(dk) == DESKCLOK_PAYLEN, "a Desk Clock payload is 32 bytes")
    pk = parse_deskclok(dk)
    ok(pk["face_name"] == "Red LED" and pk["only"] and pk["secs"]
       and pk["h24"] and pk["alarm"] and (pk["ah"], pk["am"]) == (21, 0)
       and pk["label"] == "Back up files", "a Desk Clock payload round-trips")
    ok(trim_payload(dk) == dk[:DESKCLOK_LABEL + len("Back up files")],
       "...and its file is its content: the label's NUL and on are left off")
    refuses(lambda: parse_deskclok(bytes(32)), "a payload with no INIT mark")
    refuses(lambda: deskclok(label="X" * 17), "a 17-character label")

    # the Desk Calendar's payload
    pay = deskcal(appts=[((2026, 10, 13), 570, "Dentist"),
                         ((2026, 10, 13), None, "Birthday"),
                         ((2026, 10, 1), 600, "Earlier", False)],
                  tasks=[("Buy milk", False), ("Call", True, (2026, 10, 20))])
    ok(len(pay) == DESKCAL_PAYLEN == 968, "the payload is 968 bytes")
    full = dat(pay, x=400, y=40, screen=(640, 480), payver=DESKCAL_PAYVER)
    ok(len(full) == 984, "the whole file is 984 bytes: two sectors, ONE "
       "cluster on a 360KB disk")
    dc = parse_deskcal(parse_dat(full, DESKCAL_PAYLEN,
                                 DESKCAL_PAYVER)["payload"])
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
    # v2: DUE, SNOOZED and the snooze slot (+4 minute, +6 date)
    ok(dc["snooze"] is None and not any(a["due"] or a["snoozed"]
                                        for a in dc["appts"]),
       "a v1-shaped payload: nothing due, nothing snoozed, no slot")
    p2 = deskcal(appts=[((2026, 10, 13), 1260, "Back up", True, True, 0,
                         False, True),
                        ((2026, 10, 13), 540, "Sync", True, True, 0, True)],
                 snooze=((2026, 10, 13), 1265))
    ok(struct.unpack_from("<HH", p2, DESKCAL_SNZT)
       == (1265, pack_date(2026, 10, 13)), "the slot lands at +4/+6")
    d2 = parse_deskcal(p2)
    ok(d2["appts"][0]["due"] and not d2["appts"][0]["snoozed"]
       and d2["appts"][1]["snoozed"] and not d2["appts"][1]["due"],
       "DUE and SNOOZED round-trip, each on its own appointment")
    ok(p2[DESKCAL_APPT0 + 4] == APPT_REMIND | APPT_FIRED | APPT_DUE
       and p2[DESKCAL_APPT0 + DESKCAL_APPT + 4]
       == APPT_REMIND | APPT_FIRED | APPT_SNOOZED, "flags bits 2 and 3")
    ok(d2["snooze"] == {"date": [2026, 10, 13], "time": 1265},
       "the slot reads back beside the snoozed appointment")

    # Sticky Note (SPEC.md 110.13): the payload round trip and its refusals
    sp = stiknote([("Welcome\nhi\n\x01one\n\x02two", 0), ("Shop", 2)],
                  cur=1)
    ok(len(sp) == STIKNOTE_PAYLEN == 4176, "a Sticky Note payload is 4,176")
    sn = parse_stiknote(sp)
    ok(sn["n"] == 2 and sn["cur"] == 1 and sn["desk"] and sn["init"],
       "the counts and the flags back")
    ok(sn["notes"][0]["title"] == "Welcome" and sn["notes"][1]["paper"]
       == "green" and sn["notes"][1]["body"] == [],
       "a title, a paper, a note with no body")
    ok(sn["notes"][0]["body"] == [(None, "hi"), (False, "one"),
                                  (True, "two")], "the boxes, open and ticked")
    bad = bytearray(sp)
    bad[STIKNOTE_N] = 25
    refuses(lambda: parse_stiknote(bytes(bad)), "25 notes")
    bad = bytearray(sp)
    bad[STIKNOTE_CUR] = 2
    refuses(lambda: parse_stiknote(bytes(bad)), "the current note past them")
    bad = bytearray(sp)
    struct.pack_into("<H", bad, STIKNOTE_LENS + 4, 1)
    refuses(lambda: parse_stiknote(bytes(bad)), "a length past the count")
    refuses(lambda: stiknote([("x" * 4097, 0)]), "4,097 bytes of text")
    # the walk: a break after the last space, a hard one without, a box's
    # narrower width
    ok(stiknote_wrap("Notes stay on the desk.") ==
       [(None, True, "Notes stay on the"), (None, False, "desk.")],
       "a break after the last space that fits")
    ok(stiknote_wrap("x" * 25) == [(None, True, "x" * 21),
                                   (None, False, "xxxx")], "a hard break")
    ok(stiknote_wrap("\x01Ctrl+T toggles one more") ==
       [(False, True, "Ctrl+T toggles one"), (False, False, "more")],
       "a box line wraps at 19")

    for f_ in fails:
        print("os88stik: FAIL  %s" % f_)
    print("os88stik: selfcheck %s (%d failure(s))"
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
    ap = argparse.ArgumentParser(prog="os88stik.py", description=__doc__,
                                 formatter_class=argparse.RawTextHelpFormatter)
    sub = ap.add_subparsers(dest="cmd")
    c = sub.add_parser("cfg", help="write a SYSTEM.CFG with a desktop trailer")
    c.add_argument("-o", required=True)
    c.add_argument("--want", type=lambda v: int(v, 0), default=None,
                   help="the drivers wanted, SPEC.md 51.3's 'DW' bitmap - "
                        "HDD.DRV is bit 1")
    c.add_argument("--stik", action="append", default=[],
                   help="VOL:PATH=Caption - a Sticky App row")
    c.add_argument("--short", action="append", default=[],
                   help="VOL:PATH=Caption@kind,cell - a drag-made shortcut "
                        "('\\' at +3), placed in that cell")
    d = sub.add_parser("dat", help="write a Sticky App state file")
    d.add_argument("-o", required=True)
    d.add_argument("--x", type=int)
    d.add_argument("--y", type=int)
    d.add_argument("--screen", help="WxH it was saved on")
    d.add_argument("--payver", type=int, default=1)
    d.add_argument("--paylen", type=int, default=0)
    d.add_argument("--deskcal", action="store_true",
                   help="an EMPTY Desk Calendar payload (v2, 968 bytes)")
    d.add_argument("--cont", action="store_true",
                   help="x,y are a widget's FACE origin (STIK_HF_CONT)")
    u = sub.add_parser("dump", help="a SYSTEM.CFG or a .DAT, as JSON")
    u.add_argument("file")
    u.add_argument("--paylen", type=int)
    u.add_argument("--payver", type=int, default=1)
    a = ap.parse_args(argv)

    if a.cmd == "cfg":
        rows = []
        for spec in a.stik:
            where, cap = spec.split("=", 1)
            vol, path = _path(where)
            rows.append(stik_row(vol, path, cap))
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
        print("os88stik: %s %d bytes, %d row(s), %d Sticky App"
              % (a.o, len(data), len(rows), len(a.stik)))
        return 0
    if a.cmd == "dat":
        if a.deskcal:
            payload, payver = deskcal(), DESKCAL_PAYVER
        else:
            payload, payver = bytes(a.paylen), a.payver
        screen = tuple(int(v) for v in a.screen.split("x")) if a.screen \
            else None
        data = dat(payload, x=a.x, y=a.y, screen=screen, payver=payver,
                   cont=a.cont)
        open(a.o, "wb").write(data)
        print("os88stik: %s %d bytes" % (a.o, len(data)))
        return 0
    if a.cmd == "dump":
        data = open(a.file, "rb").read()
        if data[:8] == CFG_SIG:
            out = parse_cfg(data)
            out["head"] = len(out["head"])
        else:
            n = a.paylen if a.paylen is not None else len(data) - STIK_HDR_SIZE
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
