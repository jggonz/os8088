#!/usr/bin/env python3
"""Stage a directory of Game Boy cartridges for an os8088 disk: 8.3 names.

    python3 tools/gbromdir.py SRC_DIR OUT_DIR

docs/REALDMG-SPEC.md section 18's hard disk (`make realdmghd GBROMS=dir`).
A ROM set's files are named for people - "Pokemon - Yellow Version (UE)
[C][!].gbc" - and a FAT volume takes eight and three, so each *.gb and *.gbc
in SRC_DIR is copied to OUT_DIR under a name made from the title in its own
header (0134h): its letters and digits, eight at most, POKEMONY.GBC, with a
digit put in its last place when two titles come out the same. ROMS.TXT
beside them says which is which.

The cartridges are the user's and are never written anywhere but OUT_DIR,
which is under build/ and so never committed (CLAUDE.md: never commit a
binary). An empty or missing SRC_DIR stages nothing and says so.
"""
import os
import re
import shutil
import sys

TYPES = {0x00: "ROM", 0x01: "MBC1", 0x02: "MBC1+RAM", 0x03: "MBC1+RAM+BATT",
         0x05: "MBC2", 0x06: "MBC2+BATT", 0x0F: "MBC3+TIMER+BATT",
         0x10: "MBC3+TIMER+RAM+BATT", 0x11: "MBC3", 0x12: "MBC3+RAM",
         0x13: "MBC3+RAM+BATT", 0x19: "MBC5", 0x1A: "MBC5+RAM",
         0x1B: "MBC5+RAM+BATT", 0x1C: "MBC5+RUMBLE", 0x1D: "MBC5+RUMBLE+RAM",
         0x1E: "MBC5+RUMBLE+RAM+BATT"}


def title(rom):
    t = rom[0x134:0x144] if rom[0x143] < 0x80 else rom[0x134:0x143]
    return t.split(b"\0")[0].decode("latin-1")


def stem(t, fallback):
    s = re.sub(r"[^A-Z0-9]", "", t.upper())[:8]
    return s or re.sub(r"[^A-Z0-9]", "", fallback.upper())[:8] or "GAME"


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__.strip().split("\n\n")[0] + "\n\n    usage: gbromdir.py SRC_DIR OUT_DIR")
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    files = sorted(f for f in os.listdir(src)
                   if f.lower().endswith((".gb", ".gbc"))) if os.path.isdir(src) else []
    if not files:
        print("gbromdir: no *.gb or *.gbc in %r - no GAMEBOY folder" % src)
        return 0
    used, rows = set(), []
    for f in files:
        rom = open(os.path.join(src, f), "rb").read()
        if len(rom) < 0x150:
            print("gbromdir: %s is too short for a cartridge - left out" % f)
            continue
        t = title(rom)
        ext = ".GBC" if f.lower().endswith(".gbc") else ".GB"
        base = stem(t, os.path.splitext(f)[0])
        name, n = base + ext, 1
        while name in used:
            name = base[:7] + str(n) + ext
            n += 1
            if n > 9:
                name = base[:6] + str(n) + ext
        used.add(name)
        shutil.copyfile(os.path.join(src, f), os.path.join(out, name))
        cgb = {0x80: "GB/GBC", 0xC0: "GBC only"}.get(rom[0x143], "GB")
        rows.append("%-12s %-16s %-6s %-20s %5dK  %s" % (
            name, t, cgb, TYPES.get(rom[0x147], "type %02Xh" % rom[0x147]),
            len(rom) // 1024, f.encode("ascii", "replace").decode()))
    txt = ["ROMS.TXT - the cartridges on this disk, by the name each was given",
           "(tools/gbromdir.py: the title in its header, eight letters).", "",
           "%-12s %-16s %-6s %-20s %6s  %s" % ("FILE", "TITLE", "FOR", "CARTRIDGE",
                                               "SIZE", "FROM"), ""] + rows
    with open(os.path.join(out, "ROMS.TXT"), "wb") as o:
        o.write(("\r\n".join(txt) + "\r\n").encode("ascii"))
    print("gbromdir: %d cartridges staged in %s" % (len(rows), out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
