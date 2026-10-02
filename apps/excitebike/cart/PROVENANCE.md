# 8BitBike cartridge source provenance

Importer version: 1. Source: NES-Games-Disassembly/Excitebike, commit `df2c8e5`.
Refresh: `EXCITEBIKE_REF=/local/Excitebike make excitebike-import`.
The directory path and import time are deliberately absent from generated output.

| source | bytes | SHA-256 |
|---|---:|---|
| `CHR_ROM.chr` | 8192 | `3c1bf416113e35d0b3c9151721acaa22aebbf02a69dcc5bc5faf11869d50b76f` |
| `bank_FF.asm` | 564518 | `faec8a3abb86e641d43bafe504b2582a9cfc78c28a43983fc736150c8b323dd5` |
| listing-rebuilt PRG (never committed) | 16384 | `9e1cca6ba64855acb98bb9ffa422c0b2d842c7dd9a2f701c15c7ac66651c813d` |

`header.bin` + rebuilt PRG + CHR SHA-1: `2E9897846E54A4A9865E87DE7517C6710BDEC255`.
The reader validates this full-cartridge digest, including the NROM header.

Wave 0 imports raw rules, five course streams, and 36 pieces. These are source
for later waves; the running package still uses the original-art baseline.
No artwork or screen lines are imported in wave 0, so no mark tiles or lines
have been replaced yet. G1–G4 must be applied before imported graphics ship.

Labels, lengths and consuming routines are documented in `rules.inc` and
`reference/excitebike/README.md`. Pointer values in rule tables remain 6502
CPU addresses for review; the native port must map them to its own handlers.

| generated file | SHA-256 |
|---|---|
| `pieces.txt` | `7ae4472f6c6219f006e0296eae1ee59653be836c8364a2cf3faa201cf754b641` |
| `rules.inc` | `b25c10fb0061466ec71d386d01a2d0cd5faeaedfe7bfbaa71980299995cecd06` |
| `tracks/t1.txt` | `7513f7b73fc79af394a4f834150ffdaf011aa81febbbc2e34eb4d751fb3161c5` |
| `tracks/t2.txt` | `1cda77e935ca3fda792b622626af61ad9e1f28629165a5af498cf102ce455941` |
| `tracks/t3.txt` | `328a799498e6cc90d4fa91060fc66b5bd76610624cda700c9eb273123e8758c2` |
| `tracks/t4.txt` | `bbe7be6fe043720464a3add67d1a87df2bf09b91dd183bf74146cd15ffd0a0be` |
| `tracks/t5.txt` | `1ba44608663af5ee8bae835cf7f0d40fe91cf5f537f59fab278dacb90ee7850a` |
