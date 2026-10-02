#!/usr/bin/env python3
"""Pinned listing-reader failures and an independent reader of wave 0 text.

Runs without a reference checkout or host C compiler. Only the separately
registered excitebikeimport drift row touches EXCITEBIKE_REF.
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from exbref import parse_listing
from excitebike_import import check as check_drift
sys.path.insert(0, str(Path(__file__).resolve().parent))
from harness import check, eq, done


def refusal(source, wanted, size=3):
    try:
        parse_listing(source, size=size)
    except SystemExit as error:
        return wanted in str(error)
    return False


environment = dict(os.environ)
environment.pop("EXCITEBIKE_REF", None)
command = [sys.executable, str(ROOT / "tools/excitebike_import.py")]
absent = subprocess.run(command + ["--check"], env=environment, capture_output=True, text=True)
check(absent.returncode == 0 and "SKIP" in absent.stdout,
      "optional drift row without reference reports SKIP, including env-unset machines")
refresh = subprocess.run(command, env=environment, capture_output=True, text=True)
check(refresh.returncode != 0 and "EXCITEBIKE_REF is required" in refresh.stderr,
      "explicit refresh without reference refuses with setup instruction")
with tempfile.TemporaryDirectory(prefix="exb-pin-test-") as directory:
    (Path(directory) / "CHR_ROM.chr").write_bytes(b"wrong source")
    environment["EXCITEBIKE_REF"] = directory
    bad = subprocess.run(command + ["--check"], env=environment, capture_output=True, text=True)
    check(bad.returncode != 0 and "CHR_ROM.chr: pin mismatch" in bad.stderr,
          "negative control: a present but unpinned reference fails rather than skips")


row = "- D 2 - - - 0x000010 00:C000: 01        .byte $01, $02, $03"
prg, labels = parse_listing("first:\nalias:\n" + row, size=3)
eq(prg, b"\x01\x02\x03", "multi-byte operands fill bytes absent from listing column")
eq(labels, {"first": 0xC000, "alias": 0xC000}, "label aliases share an address")
duplicate = "\n- D 2 - - - 0x000011 00:C001: 02        .byte $02"
eq(parse_listing(row + duplicate, size=3)[0], prg, "identical overlapping bytes are accepted")
check(refusal(row + duplicate.replace("02        .byte $02", "04        .byte $04"),
              "conflicting byte at $C001"), "negative control: overlapping byte conflict names address")
check(refusal(row.replace(".byte $01", ".byte $04"), "operands disagree"),
      "negative control: .byte operand and listing disagree")
check(refusal(row.replace(", $02, $03", ""), "missing listing byte at $C001"),
      "negative control: incomplete PRG is refused")
check(refusal(row.replace("C000:", "BFFF:"), "outside PRG"),
      "negative control: out-of-bounds address is refused")
check(refusal(row.replace("$02", "< next"), "unsupported .byte operands"),
      "negative control: unresolved multi-byte operands are refused")
eq(parse_listing("- D 2 - - - 0x000010 00:C000: 01        .byte < elsewhere", size=1)[0],
   b"\x01", "resolved single-byte symbolic listing uses its assembler byte")

with tempfile.TemporaryDirectory(prefix="exb-drift-test-") as directory:
    path = Path(directory) / "rules.inc"
    path.write_text("correct\n")
    check_drift(directory, {"rules.inc": "correct\n"})
    path.write_text("one-bit edit\n")
    try:
        check_drift(directory, {"rules.inc": "correct\n"})
        failed = False
    except SystemExit as error:
        failed = "rules.inc" in str(error) and "make excitebike-import" in str(error)
    check(failed, "negative control: drift identifies file and refresh command")
    path.write_text("correct\n")
    adaptations = Path(directory) / "ADAPTATIONS.md"
    adaptations.write_text("Hand-reviewed measurements may change.\n")
    check_drift(directory, {"rules.inc": "correct\n"})
    check(True, "hand-maintained adaptations are outside generated-file drift")
    extra = Path(directory) / "tracks" / "t6.txt"
    extra.parent.mkdir()
    extra.write_text("unexpected\n")
    try:
        check_drift(directory, {"rules.inc": "correct\n"})
        failed = False
    except SystemExit as error:
        failed = "t6.txt" in str(error) and "unexpected imported source" in str(error)
    check(failed, "negative control: unexpected cartridge source is refused")

# Read the text grammar ourselves; do not call importer decoding functions.
cart = ROOT / "apps/excitebike/cart"
piece_columns, piece_bytes = {}, {}
current = None
for line in (cart / "pieces.txt").read_text().splitlines():
    if not line or line.startswith("#"):
        continue
    if line.startswith("piece "):
        head = line.split()
        current = int(head[1], 16)
        piece_columns[current], piece_bytes[current] = [], []
        continue
    values = bytes.fromhex(line)
    piece_bytes[current].extend(values)
    if values[0]:
        check(len(values) == 16 - values[0], f"piece {current:02X}: column start row fits 15-row band")
        piece_columns[current].append(values)
    else:
        eq(values, b"\0", f"piece {current:02X}: terminator is a standalone column")
eq(sorted(piece_columns), list(range(36)), "all 36 pointer-table entries are preserved")
eq(piece_bytes[2], piece_bytes[3], "02/03 shared piece is preserved")
eq(piece_bytes[29], piece_bytes[30], "1D/1E shared piece is preserved")
eq(len(piece_columns[1]), 9, "piece 01 extends through its internal BIT-constant label")

repeat_pieces = [0, 0x20, 0x18, 0x1A, 0x1C, 0x22, 0x16]
lengths = [130, 121, 145, 213, 188]
column_counts = [(667, 706), (604, 638), (730, 750), (746, 797), (650, 674)]
for track in range(1, 6):
    raw = bytes(int(line.split("#", 1)[0].strip(), 16)
                for line in (cart / f"tracks/t{track}.txt").read_text().splitlines()
                if line.split("#", 1)[0].strip())
    eq(len(raw), lengths[track - 1], f"course {track}: raw label span length")
    totals = []
    for second_pass in (False, True):
        offset, total, finished = 1, 0, False
        while offset < len(raw):
            token = raw[offset]
            offset += 1
            if token & 128 and not second_pass:
                continue
            if token & 64:
                piece = repeat_pieces[token & 15]
                count = raw[offset] & 127
                offset += 1
            else:
                piece, count = token & 63, 1
            if piece in (0x30, 0x31):
                continue
            total += len(piece_columns[piece]) * count
            if piece == 9:
                finished = True
                break
        check(finished and offset == len(raw), f"course {track}: finish closes the exact raw stream")
        totals.append(total)
    eq(tuple(totals), column_counts[track - 1], f"course {track}: independent plain / second-pass columns")

tables, label = {}, None
for line in (cart / "rules.inc").read_text().splitlines():
    if line.endswith(":"):
        label = line[:-1]
        tables[label] = bytearray()
    elif line.strip().startswith("db "):
        tables[label].extend(int(value.strip(), 16) for value in line.strip()[3:].split(","))
eq(len(tables["tbl_D8CD"]), 16, "AI RNG nibble indexes sixteen bytes across adjacent tables")
eq(tables["tbl_D8CD"][8:11], tables["tbl_D8D5"], "D8CD preserves integer-speed table overlap")
eq(tables["tbl_D87C"][-1], tables["tbl_D88B"][0], "landing upper-bound table preserves final overlapping byte")
eq(len(tables["tbl_E6B7"]), 42, "handler dispatch has 21 slots, including unused entries")
eq(len(tables["tbl_C13C"]), 6, "track queue excludes adjacent garbage")
eq(tables["tbl_C091"][-2:], tables["tbl_C0B0"][:2], "design par record preserves shared ring bytes")
done("excitebike import reader")
