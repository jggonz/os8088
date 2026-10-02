#!/usr/bin/env python3
"""Refresh readable cartridge source for 8BitBike, outside the build path.

EXCITEBIKE_REF=/local/Excitebike python3 tools/excitebike_import.py
--check compares generated files without changing them; --selfcheck repeats the
import and checks every raw course and piece. Later waves add art and sound.
"""
import argparse
import hashlib
import os
from pathlib import Path
import tempfile

import exbref

VERSION = 1
ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = ROOT / "apps/excitebike/cart"
# Lengths follow the consumer, not the next label. Especially D8CD (RNG & 15),
# C091 (eleven three-byte records), and the landing tables (+8 slow-speed bank).
# Pointer tables preserve 6502 addresses as evidence; a native port must remap.
RULES = (
    ("tbl_C000", 28, "sub_C2A9", "14 state dispatch words; cartridge CPU addresses"),
    ("tbl_C091", 33, "sub_C522", "11 three-byte par records; final record overlaps tbl_C0B0"),
    ("tbl_C0B0", 4, "ofs_001_C875_02_06", "initial collision-ring columns by bike"),
    ("tbl_C0B4_default_position", 4, "ofs_001_C875_02_06", "lane centres by bike"),
    ("tbl_C0B8", 4, "ofs_001_C875_02_06", "initial screen x by bike"),
    ("tbl_C0BC", 5, "sub_CE29", "fractional acceleration; selected by CD59, once per four-frame cadence"),
    ("tbl_C0C1", 7, "sub_CE58 / sub_CE5C", "deceleration including mud B-held C0 and released 7F"),
    ("tbl_C0C8", 2, "loc_CE83", "Right pitch targets indexed by air state >> 1"),
    ("tbl_C0CA", 2, "loc_CE83", "Left pitch maxima indexed by air state >> 1"),
    ("tbl_C0CC", 2, "sub_CE5C", "low-speed deceleration threshold, ground / air"),
    ("tbl_C0CE", 3, "sub_CD59 / sub_CE29", "speed cap fractional bytes, A / B / finish"),
    ("tbl_C0D1", 3, "sub_CD59 / sub_CE29", "speed cap integer bytes, A / B / finish"),
    ("tbl_C0D4", 2, "loc_CE83", "pitch-step frame reload, ground / air"),
    ("tbl_C0EC_ppu_address", 10, "sub_C94B", "five big-endian PPU queue destinations"),
    ("tbl_C134", 8, "bra_CC3A_loop", "rank cursor queue, copied in reverse byte order"),
    ("tbl_C13C", 6, "ofs_001_C5AC_04_03", "track number queue; excludes five trailing garbage bytes"),
    ("tbl_C158", 8, "ofs_001_C875_02_06", "HUD queue patch copied by C8EA"),
    ("tbl_C160", 6, "sub_CA9B", "finish-flash palette cycle"),
    ("tbl_C166", 4, "sub_CA9B", "finish-flash palette cycle; includes C168/C169"),
    ("tbl_C16A", 6, "sub_CA9B", "finish-flash palette cycle"),
    ("tbl_C170", 4, "sub_CA9B", "finish-flash palette cycle; includes C172/C173"),
    ("tbl_D868", 4, "loc_DD06", "gravity indexed by pad & 3"),
    ("tbl_D86C", 16, "sub_DC1A", "landing pitch lower bounds; low-speed bank at +8"),
    ("tbl_D87C", 16, "sub_DC1A", "exclusive upper bounds; last byte overlaps tbl_D88B"),
    ("tbl_D88B", 7, "sub_DCA0", "perfect pitch by slope class"),
    ("tbl_D8C4", 5, "ofs_003_D953_02 / ofs_003_D9F6_05 / sub_DDD1", "recovery lane targets"),
    ("tbl_D8CD", 16, "sub_DB50", "RNG & 0F indexes across D8D5, D8D8 and first D8DC byte"),
    ("tbl_D8D5", 3, "sub_DB50", "AI target speed integer by spawn type"),
    ("tbl_D8D8", 4, "sub_DAE3", "respawn lane centres"),
    ("tbl_D8DF", 3, "sub_DAE3", "respawn screen x base"),
    ("tbl_D8E2", 3, "sub_DAE3", "respawn collision-ring offset added to player column, then masked with 3F"),
    ("tbl_D8F7", 4, "sub_E359_display_temperature_meter_with_sprites", "heat fractional increments by throttle mode"),
    ("tbl_D8FB", 4, "sub_E359_display_temperature_meter_with_sprites", "heat integer targets by throttle mode"),
    ("tbl_D8FF_lo", 6, "sub_D924", "crash-phase dispatch low bytes; cartridge CPU addresses"),
    ("tbl_D905_hi", 6, "sub_D924", "crash-phase dispatch high bytes; cartridge CPU addresses"),
    ("tbl_D90E", 5, "ofs_003_D933_01", "thrown rider recovery distance by speed class"),
    ("tbl_D913", 5, "sub_DFB2", "lane-band boundaries"),
    ("tbl_E6AD", 10, "bra_E7A3", "slope class for absolute pitch minus 2"),
    ("tbl_E6B7", 42, "sub_E794", "21 handler dispatch words; cartridge CPU addresses"),
)
TRACK_LABELS = (
    "_off_003_ED46_00", "_off_003_EE59_01", "_off_003_EDC8_02",
    "_off_003_EED2_03", "_off_003_EFA7_04",
)


def pieces(reader):
    """Walk F681's (start-row, 15-start-row tiles) columns, then header 00.

    A BIT constant label sits inside piece 01. Adjacent labels therefore cannot
    delimit pieces. The pointer table also aliases 02/03 and 1D/1E.
    """
    lo, hi = reader.table("tbl_F063_lo", 36), reader.table("tbl_F087_hi", 36)
    result = []
    for index in range(36):
        start = lo[index] | hi[index] << 8
        address, columns = start, []
        while True:
            top = reader.read(address, 1)[0]
            address += 1
            if top == 0:
                break
            if not 1 <= top < 15:
                raise SystemExit(f"piece {index:02X}: bad row {top:02X} at ${address - 1:04X}")
            tiles = reader.read(address, 15 - top)
            columns.append((top, tiles))
            address += len(tiles)
            if len(columns) > 256:
                raise SystemExit(f"piece {index:02X}: no terminating column")
        result.append((start, columns, reader.read(start, address - start)))
    return result


def course_entries(data, repeat_map):
    """Annotate raw bytes with F4FF's grammar; finish piece 09 ends a lap.

    F545 skips a gated byte only. Count bytes are masked with 7F at F58D;
    bit 7 in a count is not a second-pass condition.
    """
    if len(data) < 2:
        raise SystemExit("course stream lacks lap count and pieces")
    result = [(data[0], "lap count")]
    offset = 1
    while offset < len(data):
        value = data[offset]
        offset += 1
        gate = "; skipped on plain pass" if value & 0x80 else ""
        if value == 0:
            result.append((value, "stop"))
            break
        if value & 0x40:
            piece = repeat_map[value & 15]
            if offset >= len(data):
                raise SystemExit("course stream ends in a repeat opcode")
            result.append((value, f"repeat piece {piece:02X}" + gate))
            count = data[offset]
            offset += 1
            result.append((count, f"repeat count {count & 127}; high bit ignored"))
        else:
            piece = value & 63
            description = {0x30: "enable fallen-rider marker before finish",
                           0x31: "disable fallen-rider marker", 9: "finish gate; restart lap"}.get(
                               piece, f"piece {piece:02X}")
            result.append((value, description + gate))
            if piece == 9:
                break
    if offset != len(data):
        raise SystemExit(f"course stream has {len(data) - offset} bytes after terminator")
    if result[-1][0] != 0 and (result[-1][0] & 63) != 9:
        raise SystemExit("course stream lacks finish gate or stop")
    return result


def render(reader):
    reader.selfcheck()
    imported = {}
    rule_lines = ["; 8BitBike cartridge rules, importer v1. SPEC.md §102.",
                  "; Generated by tools/excitebike_import.py; refresh, do not edit.",
                  "; Raw 6502 dispatch addresses are evidence, not native callable pointers.", ""]
    for label, length, consumer, meaning in RULES:
        reader.address(consumer.split(" / ")[0])
        rule_lines += [f"; {label} (${reader.address(label):04X}, {length} bytes; {consumer})",
                       f"; {meaning}", label + ":"]
        values = reader.table(label, length)
        for offset in range(0, len(values), 16):
            rule_lines.append("    db " + ", ".join(f"0x{byte:02X}" for byte in values[offset:offset + 16]))
        rule_lines.append("")
    imported["rules.inc"] = "\n".join(rule_lines)
    repeat_map = reader.table("tbl_F4C6", 16)
    for index, label in enumerate(TRACK_LABELS, 1):
        values = reader.stream(label)
        lines = [f"# 8BitBike course {index}: {label} at ${reader.address(label):04X}",
                 "# Raw stream bytes in cartridge order; consumed by sub_F4FF.",
                 "# A count byte belongs to its preceding repeat opcode; finish 09 restarts the lap."]
        lines += [f"{byte:02X} # {offset:03d}: {meaning}" for offset, (byte, meaning)
                  in enumerate(course_entries(values, repeat_map))]
        imported[f"tracks/t{index}.txt"] = "\n".join(lines) + "\n"
    piece_data = pieces(reader)
    lines = ["# 8BitBike: 36 cartridge track pieces from tbl_F063_lo / tbl_F087_hi.",
             "# sub_F82E_prepare_pointers selects; sub_F681 expands a column.",
             "# piece ID address=CPU_ADDRESS columns=N", "# Each column: start-row followed by 15-start-row tile IDs.",
             "# Rows below start-row begin as FC (sub_F676); final 00 terminates the piece.",
             "# Tiles are raw pattern-table-1 IDs. Cartridge track rules are applied later.", ""]
    for index, (address, columns, raw) in enumerate(piece_data):
        lines.append(f"piece {index:02X} address={address:04X} columns={len(columns)}")
        lines += [f"{top:02X} " + " ".join(f"{byte:02X}" for byte in tiles) for top, tiles in columns]
        lines += ["00", ""]
    imported["pieces.txt"] = "\n".join(lines)
    provenance = ["# 8BitBike cartridge source provenance", "", f"Importer version: {VERSION}. Source: NES-Games-Disassembly/Excitebike, commit `{exbref.COMMIT}`.",
                  "Refresh: `EXCITEBIKE_REF=/local/Excitebike make excitebike-import`.",
                  "The directory path and import time are deliberately absent from generated output.", "",
                  "| source | bytes | SHA-256 |", "|---|---:|---|"]
    provenance += [f"| `{name}` | {size} | `{digest}` |" for name, (size, digest) in exbref.PINS.items()]
    provenance += [f"| listing-rebuilt PRG (never committed) | {exbref.SIZE} | `{exbref.PRG_SHA256}` |", "",
                   f"`header.bin` + rebuilt PRG + CHR SHA-1: `{exbref.CARTRIDGE_SHA1}`.",
                   "The reader validates this full-cartridge digest, including the NROM header.", "",
                   "Wave 0 imports raw rules, five course streams, and 36 pieces. These are source",
                   "for later waves; the running package still uses the original-art baseline.",
                   "No artwork or screen lines are imported in wave 0, so no mark tiles or lines",
                   "have been replaced yet. G1–G4 must be applied before imported graphics ship.", "",
                   "Labels, lengths and consuming routines are documented in `rules.inc` and",
                   "`reference/excitebike/README.md`. Pointer values in rule tables remain 6502",
                   "CPU addresses for review; the native port must map them to its own handlers.", "",
                   "| generated file | SHA-256 |", "|---|---|"]
    provenance += [f"| `{name}` | `{hashlib.sha256(content.encode()).hexdigest()}` |"
                   for name, content in sorted(imported.items())]
    imported["PROVENANCE.md"] = "\n".join(provenance) + "\n"
    return imported


def write(output, imported):
    output = Path(output)
    for name, content in sorted(imported.items()):
        path = output / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content.encode("utf-8"))


def check(output, imported):
    """Compare only importer-owned files: ADAPTATIONS.md is maintained by hand."""
    output = Path(output)
    if output.is_dir():
        present = {path.relative_to(output).as_posix() for path in output.rglob("*") if path.is_file()}
        unexpected = sorted(present - set(imported) - {"ADAPTATIONS.md"})
        if unexpected:
            raise SystemExit(f"{output / unexpected[0]}: unexpected imported source; "
                             "remove it or extend tools/excitebike_import.py and run make excitebike-import")
    for name, content in sorted(imported.items()):
        path = output / name
        if not path.is_file() or path.read_bytes() != content.encode("utf-8"):
            raise SystemExit(f"{path}: imported source drift; run make excitebike-import with EXCITEBIKE_REF")


def selfcheck(reader):
    first, second = render(reader), render(exbref.Reader(reader.path))
    if first != second:
        raise SystemExit("excitebike importer: two imports differ")
    with tempfile.TemporaryDirectory(prefix="excitebike-import-") as directory:
        left, right = Path(directory) / "first", Path(directory) / "second"
        write(left, first)
        write(right, second)
        check(left, second)
        check(right, first)
    print(f"excitebike importer: two byte-identical runs; {len(RULES)} rule tables, five courses, 36 pieces")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-o", "--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--check", action="store_true", help="refuse source drift without writing")
    parser.add_argument("--selfcheck", action="store_true", help="prove pins and deterministic import without writing")
    args = parser.parse_args()
    if args.check and not args.selfcheck and not os.environ.get("EXCITEBIKE_REF"):
        print("excitebike importer: SKIP (EXCITEBIKE_REF is not set; committed sources build independently)")
        return
    reader = exbref.Reader()
    if args.selfcheck:
        selfcheck(reader)
    elif args.check:
        check(args.output, render(reader))
        print("excitebike importer: committed sources match the pinned reference")
    else:
        imported = render(reader)
        write(args.output, imported)
        print(f"excitebike importer: {len(imported)} text files -> {args.output}")


if __name__ == "__main__":
    main()
