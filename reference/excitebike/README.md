# 8BitBike cartridge reference

The offline source is `NES-Games-Disassembly/Excitebike` at commit `df2c8e5`.
`EXCITEBIKE_REF` names its local directory. This directory holds notes and pins
only; no cartridge, CHR image, emulator or disassembly is committed here.
The native package builds entirely from committed sources in `apps/excitebike/`.

| input | bytes | SHA-256 |
|---|---:|---|
| `CHR_ROM.chr` | 8,192 | `3c1bf416113e35d0b3c9151721acaa22aebbf02a69dcc5bc5faf11869d50b76f` |
| `bank_FF.asm` | 564,518 | `faec8a3abb86e641d43bafe504b2582a9cfc78c28a43983fc736150c8b323dd5` |
| PRG rebuilt in memory from the listing | 16,384 | `9e1cca6ba64855acb98bb9ffa422c0b2d842c7dd9a2f701c15c7ac66651c813d` |

The cartridge is NROM-128, mapper 0, with vertical nametable mirroring.
`header.bin` + rebuilt PRG + CHR has SHA-1
`2E9897846E54A4A9865E87DE7517C6710BDEC255`, matching `assemble.sh`.
`tools/exbref.py --selfcheck` proves both source pins, full listing coverage,
PRG SHA-256 and cartridge SHA-1 without assembling or writing a binary.
Multi-byte `.byte` operands must be read: their listing column shows only the
first byte. Conflicting overlapping writes and missing bytes are refused.

`make excitebike-import` refreshes readable source. `tools/excitebike_import.py
--check` names a drifted or unexpected file and the refresh command.
`--selfcheck` repeats the import and compares the complete output byte for byte.
Neither the build nor the ordinary regression rows uses the reference checkout.
The optional `excitebikeimport` row reports SKIP when the reference is absent.

Wave 0 imports 39 rule tables, five raw course streams and all 36 piece entries.
The native game still runs its engine baseline. Importing a table is not evidence
that its consuming routine has been ported; the later waves' recorded-cartridge
runtime gates establish that. `cart/ADAPTATIONS.md` records the current status.

The critical boundaries come from the routines that consume these sources:

- `tbl_C091` at `$C091` contains ten full par records and one single-byte
  design record. `sub_C522` at `$C522` reads three bytes per record, so its
  eleventh read includes the first two bytes of `tbl_C0B0` at `$C0B0`.
- `tbl_C13C` at `$C13C` has a six-byte queue copied by
  `ofs_001_C5AC_04_03` at `$C5AC`. The five unlabelled bytes following it are
  garbage and must not become part of this queue.
- `tbl_D86C` at `$D86C` and `tbl_D87C` at `$D87C` are landing lower and
  exclusive upper bounds, selected in `sub_DC1A` at `$DC1A`. Speeds below 2
  add 8 to the slope index. The upper table's 16th address is also
  `tbl_D88B`'s first byte. Its normal seven slope targets start at `$D88B`.
- `tbl_D8CD` at `$D8CD` has eight labelled fractional speeds, but
  `sub_DB50` at `$DB50` masks RNG with `$0F` at `$DB63`. Preserve sixteen
  bytes, including the adjacent integer speeds, respawn lane centres and
  first OAM-index offset. Reducing this to eight values changes the AI.
- `tbl_E6B7` at `$E6B7` is 21 little-endian dispatch words through `$E6E0`,
  selected by `sub_E794` at `$E794`. These are 6502 CPU addresses, not native
  pointers. `tbl_E6AD` at `$E6AD` maps absolute pitch minus 2 to slope class
  in `bra_E7A3` at `$E7A3`.
- `tbl_ED3A_lo` at `$ED3A` / `tbl_ED40_hi` at `$ED40` order the five course
  streams as `$ED46`, `$EE59`, `$EDC8`, `$EED2`, `$EFA7`. Their label spans
  contain 130, 121, 145, 213 and 188 bytes, respectively.
  `sub_F4FF` at `$F4FF` first reads a lap count, then pieces, repeat opcodes
  and spawn markers. Bit 7 skips a single opcode on the plain pass; bit 6
  selects a repeat and its count byte, masked with `$7F`. A finish piece
  `$09` restarts the lap in `loc_F62D` at `$F62D`; these five streams end
  there, without a zero terminator.
- `tbl_F063_lo` at `$F063` / `tbl_F087_hi` at `$F087` contain 36 piece
  pointers. `sub_F82E_prepare_pointers` at `$F82E` selects them and
  `sub_F681` at `$F681` expands each column: a starting row, then
  `15 - starting_row` tile references. A zero column header ends a piece.
  Piece 01 includes the internal BIT-constant label `tbl_F0B5_40` at
  `$F0B5`; stopping at the next label would truncate it. Piece pairs 02/03
  and 1D/1E alias the same streams.

Every imported rule's full label, CPU address, length, meaning and consuming
routine is recorded next to its values in `apps/excitebike/cart/rules.inc`.
The groups are speed/pitch (`$C0BC–$C0D5`, `sub_CD59`, `sub_CE29`,
`sub_CE5C`, `loc_CE83`), menu/HUD queues (`$C0EC–$C15F`), finish palette
cycles (`$C160–$C173`, `sub_CA9B`), gravity/landing (`$D868–$D891`), crash
recovery (`$D8C4`, `$D8FF–$D917`, `sub_D924`), heat (`$D8F7–$D8FE`,
`sub_E359_display_temperature_meter_with_sprites`) and AI respawn/targets
(`$D8CD–$D8E4`, `sub_DAE3`, `sub_DB50`).

The host unit row independently decodes committed pieces and streams and proves
the column totals, plain / second pass: 667/706, 604/638, 730/750, 746/797,
650/674. It also rejects synthetic reader conflicts, incomplete listing
coverage, operand disagreement and a planted source edit. These checks require
neither the local disassembly nor a host C compiler.

Wave 0 rule inventory (byte counts include the consumer’s overlapping reads):

| label | address | bytes read | consumer | meaning |
|---|---|---:|---|---|
| `tbl_C000` | `$C000` | 28 | `sub_C2A9` | 14 state dispatch words; cartridge CPU addresses |
| `tbl_C091` | `$C091` | 33 | `sub_C522` | 11 three-byte par records; final record overlaps tbl_C0B0 |
| `tbl_C0B0` | `$C0B0` | 4 | `ofs_001_C875_02_06` | initial collision-ring columns by bike |
| `tbl_C0B4_default_position` | `$C0B4` | 4 | `ofs_001_C875_02_06` | lane centres by bike |
| `tbl_C0B8` | `$C0B8` | 4 | `ofs_001_C875_02_06` | initial screen x by bike |
| `tbl_C0BC` | `$C0BC` | 5 | `sub_CE29` | fractional acceleration; selected by CD59, once per four-frame cadence |
| `tbl_C0C1` | `$C0C1` | 7 | `sub_CE58 / sub_CE5C` | deceleration including mud B-held C0 and released 7F |
| `tbl_C0C8` | `$C0C8` | 2 | `loc_CE83` | Right pitch targets indexed by air state >> 1 |
| `tbl_C0CA` | `$C0CA` | 2 | `loc_CE83` | Left pitch maxima indexed by air state >> 1 |
| `tbl_C0CC` | `$C0CC` | 2 | `sub_CE5C` | low-speed deceleration threshold, ground / air |
| `tbl_C0CE` | `$C0CE` | 3 | `sub_CD59 / sub_CE29` | speed cap fractional bytes, A / B / finish |
| `tbl_C0D1` | `$C0D1` | 3 | `sub_CD59 / sub_CE29` | speed cap integer bytes, A / B / finish |
| `tbl_C0D4` | `$C0D4` | 2 | `loc_CE83` | pitch-step frame reload, ground / air |
| `tbl_C0EC_ppu_address` | `$C0EC` | 10 | `sub_C94B` | five big-endian PPU queue destinations |
| `tbl_C134` | `$C134` | 8 | `bra_CC3A_loop` | rank cursor queue, copied in reverse byte order |
| `tbl_C13C` | `$C13C` | 6 | `ofs_001_C5AC_04_03` | track number queue; excludes five trailing garbage bytes |
| `tbl_C158` | `$C158` | 8 | `ofs_001_C875_02_06` | HUD queue patch copied by C8EA |
| `tbl_C160` | `$C160` | 6 | `sub_CA9B` | finish-flash palette cycle |
| `tbl_C166` | `$C166` | 4 | `sub_CA9B` | finish-flash palette cycle; includes C168/C169 |
| `tbl_C16A` | `$C16A` | 6 | `sub_CA9B` | finish-flash palette cycle |
| `tbl_C170` | `$C170` | 4 | `sub_CA9B` | finish-flash palette cycle; includes C172/C173 |
| `tbl_D868` | `$D868` | 4 | `loc_DD06` | gravity indexed by pad & 3 |
| `tbl_D86C` | `$D86C` | 16 | `sub_DC1A` | landing pitch lower bounds; low-speed bank at +8 |
| `tbl_D87C` | `$D87C` | 16 | `sub_DC1A` | exclusive upper bounds; last byte overlaps tbl_D88B |
| `tbl_D88B` | `$D88B` | 7 | `sub_DCA0` | perfect pitch by slope class |
| `tbl_D8C4` | `$D8C4` | 5 | `ofs_003_D953_02 / ofs_003_D9F6_05 / sub_DDD1` | recovery lane targets |
| `tbl_D8CD` | `$D8CD` | 16 | `sub_DB50` | RNG & 0F indexes across D8D5, D8D8 and first D8DC byte |
| `tbl_D8D5` | `$D8D5` | 3 | `sub_DB50` | AI target speed integer by spawn type |
| `tbl_D8D8` | `$D8D8` | 4 | `sub_DAE3` | respawn lane centres |
| `tbl_D8DF` | `$D8DF` | 3 | `sub_DAE3` | respawn screen x base |
| `tbl_D8E2` | `$D8E2` | 3 | `sub_DAE3` | respawn collision-ring offset added to player column, then masked with 3F |
| `tbl_D8F7` | `$D8F7` | 4 | `sub_E359_display_temperature_meter_with_sprites` | heat fractional increments by throttle mode |
| `tbl_D8FB` | `$D8FB` | 4 | `sub_E359_display_temperature_meter_with_sprites` | heat integer targets by throttle mode |
| `tbl_D8FF_lo` | `$D8FF` | 6 | `sub_D924` | crash-phase dispatch low bytes; cartridge CPU addresses |
| `tbl_D905_hi` | `$D905` | 6 | `sub_D924` | crash-phase dispatch high bytes; cartridge CPU addresses |
| `tbl_D90E` | `$D90E` | 5 | `ofs_003_D933_01` | thrown rider recovery distance by speed class |
| `tbl_D913` | `$D913` | 5 | `sub_DFB2` | lane-band boundaries |
| `tbl_E6AD` | `$E6AD` | 10 | `bra_E7A3` | slope class for absolute pitch minus 2 |
| `tbl_E6B7` | `$E6B7` | 42 | `sub_E794` | 21 handler dispatch words; cartridge CPU addresses |
