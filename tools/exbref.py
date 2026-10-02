#!/usr/bin/env python3
"""Pinned, read-only Excitebike disassembly reader (SPEC.md §102).

Only authoring/refresh tools use this module. No assembler, emulator, network,
or binary output is required. EXCITEBIKE_REF names the local Excitebike directory.
"""
import argparse
import hashlib
import os
from pathlib import Path
import re

COMMIT = "df2c8e5"
PINS = {
    "CHR_ROM.chr": (8192, "3c1bf416113e35d0b3c9151721acaa22aebbf02a69dcc5bc5faf11869d50b76f"),
    "bank_FF.asm": (564518, "faec8a3abb86e641d43bafe504b2582a9cfc78c28a43983fc736150c8b323dd5"),
}
PRG_SHA256 = "9e1cca6ba64855acb98bb9ffa422c0b2d842c7dd9a2f701c15c7ac66651c813d"
CARTRIDGE_SHA1 = "2E9897846E54A4A9865E87DE7517C6710BDEC255"
BASE, SIZE = 0xC000, 0x4000
LISTING = re.compile(r"^[-A-Z0-9 ]+ 0x[0-9A-F]+ 00:([0-9A-F]{4}): ((?:[0-9A-F]{2} ?)+)\s*(.*)$")
LABEL = re.compile(r"^([A-Za-z_][A-Za-z_0-9]*):(?:\s*;.*)?\s*$")


def source_path(ref=None):
    ref = ref if ref is not None else os.environ.get("EXCITEBIKE_REF")
    if not ref:
        raise SystemExit("EXCITEBIKE_REF is required: name a local Excitebike disassembly directory")
    return Path(ref)


def check_source(ref=None):
    """Validate both source pins; every refusal identifies the offending file."""
    directory = source_path(ref)
    for name, (size, digest) in PINS.items():
        path = directory / name
        try:
            data = path.read_bytes()
        except OSError as error:
            raise SystemExit(f"{path}: {error.strerror}") from error
        actual = hashlib.sha256(data).hexdigest()
        if len(data) != size or actual != digest:
            raise SystemExit(f"{path}: pin mismatch: expected {size} bytes SHA-256 {digest}; "
                             f"got {len(data)} bytes SHA-256 {actual}")
    return directory


def parse_listing(text, base=BASE, size=SIZE):
    """Reconstruct every byte, checking coverage, aliases and overlapping writes.

    Literal .byte operands are authoritative: the listing's hex column may show
    only their first byte. Symbolic single-byte operands and .word/.dbyt operands
    use the assembler's resolved listing column. This is a listing reader, not
    an assembler. Multi-byte symbolic .byte operands are refused.
    """
    data, seen = bytearray(size), bytearray(size)
    labels, pending = {}, []
    for lineno, line in enumerate(text.splitlines(), 1):
        label = LABEL.match(line)
        if label:
            pending.append(label[1])
            continue
        row = LISTING.match(line)
        if not row:
            continue
        address = int(row[1], 16)
        listed = bytes(int(item, 16) for item in row[2].split())
        operand = row[3].split(";", 1)[0].strip()
        values = listed
        if operand.startswith(".byte"):
            items = [item.strip() for item in operand[5:].split(",")]
            if all(re.fullmatch(r"\$[0-9A-Fa-f]{1,2}", item) for item in items):
                values = bytes(int(item[1:], 16) for item in items)
                if not values.startswith(listed):
                    raise SystemExit(f"bank_FF.asm:{lineno}: .byte operands disagree at ${address:04X}")
            elif len(items) != 1 or len(listed) != 1:
                raise SystemExit(f"bank_FF.asm:{lineno}: unsupported .byte operands at ${address:04X}")
        for name in pending:
            if name in labels and labels[name] != address:
                raise SystemExit(f"bank_FF.asm:{lineno}: conflicting label {name}")
            labels[name] = address
        pending.clear()
        offset = address - base
        if offset < 0 or offset + len(values) > size:
            raise SystemExit(f"bank_FF.asm:{lineno}: address ${address:04X} outside PRG")
        for index, value in enumerate(values, offset):
            if seen[index] and data[index] != value:
                raise SystemExit(f"bank_FF.asm:{lineno}: conflicting byte at ${base + index:04X}: "
                                 f"${data[index]:02X} versus ${value:02X}")
            data[index], seen[index] = value, 1
    if not all(seen):
        missing = seen.index(0)
        raise SystemExit(f"bank_FF.asm: missing listing byte at ${base + missing:04X}")
    if pending:
        raise SystemExit(f"bank_FF.asm: labels without bytes: {', '.join(pending)}")
    return bytes(data), labels


class Reader:
    def __init__(self, ref=None):
        self.path = check_source(ref)
        self._rom, self.labels = parse_listing((self.path / "bank_FF.asm").read_text(encoding="utf-8"))
        digest = hashlib.sha256(self._rom).hexdigest()
        if digest != PRG_SHA256:
            raise SystemExit(f"bank_FF.asm: rebuilt PRG SHA-256 mismatch: {digest}")
        self._boundaries = sorted(set(self.labels.values()) | {BASE + SIZE})

    def rom(self):
        """The validated 16KB PRG, never written to disk by this reader."""
        return self._rom

    def address(self, label):
        try:
            return self.labels[label]
        except KeyError as error:
            raise SystemExit(f"bank_FF.asm: missing label {label}") from error

    def read(self, address, n):
        if n < 0 or address < BASE or address + n > BASE + SIZE:
            raise SystemExit(f"bank_FF.asm: invalid PRG span ${address:04X}+{n}")
        return self._rom[address - BASE:address - BASE + n]

    def table(self, label, n):
        """Read n bytes, including deliberate reads across adjacent labels."""
        return self.read(self.address(label), n)

    def stream(self, label):
        """Bytes through the next distinct label address; aliases do not truncate.

        This does not interpret terminators. Grammars with internal labels or
        shared suffixes must walk their own records with read()/table().
        """
        address = self.address(label)
        end = next(boundary for boundary in self._boundaries if boundary > address)
        return self.read(address, end - address)

    def cartridge(self):
        """Return pinned iNES bytes in memory for authoring-time recorders only."""
        header_path = self.path / "header.bin"
        try:
            header = header_path.read_bytes()
        except OSError as error:
            raise SystemExit(f"{header_path}: {error.strerror}") from error
        if (len(header) != 16 or header[:6] != b"NES\x1a\x01\x01"
                or header[6] & 0xF0 or header[7] & 0xF0):
            raise SystemExit(f"{header_path}: expected NROM-128 header (16KB PRG, 8KB CHR, mapper 0)")
        cartridge = header + self._rom + (self.path / "CHR_ROM.chr").read_bytes()
        digest = hashlib.sha1(cartridge).hexdigest().upper()
        if digest != CARTRIDGE_SHA1:
            raise SystemExit(f"{header_path}: cartridge SHA-1 mismatch: {digest}; expected {CARTRIDGE_SHA1}")
        return cartridge

    def selfcheck(self):
        self.cartridge()
        return {"prg_bytes": len(self._rom), "labels": len(self.labels),
                "prg_sha256": PRG_SHA256, "cartridge_sha1": CARTRIDGE_SHA1}


def rom(ref=None):
    return Reader(ref).rom()


def table(label, n, ref=None):
    return Reader(ref).table(label, n)


def stream(label, ref=None):
    return Reader(ref).stream(label)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--selfcheck", action="store_true", required=True)
    args = parser.parse_args()
    if args.selfcheck:
        result = Reader().selfcheck()
        print(f"exbref: {result['prg_bytes']} PRG bytes, {result['labels']} labels; "
              f"SHA-256 {PRG_SHA256}; cartridge SHA-1 {CARTRIDGE_SHA1}")


if __name__ == "__main__":
    main()
