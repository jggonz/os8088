# DrMarco's NES reference

These two files are **Nintendo's Dr. Mario (1990)**, as disassembled by
cyneprepou4uk in [NES-Games-Disassembly](https://github.com/cyneprepou4uk/NES-Games-Disassembly)
(`Dr. Mario/`, commit `df2c8e5`), from the GoodNES dump
*Dr. Mario (JU) (PRG0) [!]*, CRC32 `B1F7E3E9`. They are neither ours nor
under this tree's licence, and they are copied here byte for byte.

| file | bytes | SHA-256 |
|---|---|---|
| `CHR_ROM.chr` | 32,768 | `853123999e15a05723cfaa1f928980d294d985dfcea5da2ad407afd56d0586a3` |
| `bank_FF.asm` | 1,084,908 | `90466e12c051d210054b3d85f314988a61aa7ca1068774400af7ba213400accb` |

`tools/drmario_assets.py` carries the same two hashes and refuses any other
bytes, naming the file, so an edit here cannot change the game silently.

## What the build takes from them

Nothing here is emulated or shipped whole. At build time (SPEC.md 100):

- **`tools/drmario_assets.py`** decodes the capsule and bottle-virus cells out
  of `CHR_ROM.chr` into the native VGA and CGA tile caches, and reads the
  speed, virus-colour, pair and height tables out of `bank_FF.asm`'s `.byte`
  lines into `dm-tables.inc`.
- **`tools/drmario_audio.py`** walks the music sequencer's note data in
  `bank_FF.asm` into bounded phrase lists for OPL2 and the speaker.

Both are stdlib Python and write only to `build/drmario-art/`.

## Why they are committed

Until this commit the build read them from a sibling checkout of
NES-Games-Disassembly and DrMarco was a `local` package: no clone without that
checkout could build it, so it rode no disk and `make` never built it. The
owner decided to commit them so that DrMarco builds from this tree alone and
ships like any other game. That is a stated departure for these two files
only, the same kind as `apps/c64/rom/`'s three Commodore images. They live
in `reference/` beside Gorillas' `gorilla.bas` rather than under `apps/`,
because `bank_FF.asm` is 6502 source and every `.asm` under `apps/` is held
to the 8086 rules (`tests/unit/t_asmrules.py`).
