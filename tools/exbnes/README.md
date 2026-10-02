# Offline NES recorder

`python3 tools/exbnes/record.py --refresh` (also `make excitebike-fixtures`)
refreshes the compact committed attract-to-race recording and its manifest.
Set `EXCITEBIKE_REF` to the verified Excitebike disassembly directory. The
refresh uses host `cc`, and fetches [agnes](https://github.com/kgabis/agnes) at
`0e4220b084c467e39c04805d955e78c463feadd0` into `build/exbnes/agnes/`, together
with its MIT licence. Nothing is fetched on the ordinary build or test path.
`--agnes-from /path/to/agnes` verifies and copies an existing local checkout;
`python3 tools/exbnes/getagnes.py --check` only checks the cache.

The recorder is our scouting `oracle.c` promoted into the repository. Its
controller script is text: `DECIMAL_REPEAT HEX_PAD1 [HEX_PAD2]`, with blank
lines and `#` comments allowed. Each byte uses the NES serial order:
A=01, B=02, Select=04, Start=08, Up=10, Down=20, Left=40, Right=80.
Malformed lines fail with their line number. No FM2 movie is needed.

For a new scenario:

```
EXCITEBIKE_REF=/path/to/Excitebike python3 tools/exbnes/record.py \
  --script scenario.pad --out build/scenario.jsonl.gz --pixels 0,60,120 --apu
```

`--selfcheck` records the script twice and compares every emitted frame,
pixel sample and APU write. `--refresh` always performs this check before
writing fixtures, and additionally proves the fixed script reaches both an
attract race and a human-controlled race. These are NES emulator results,
not measurements from physical hardware.

The JSONL frame boundary is `agnes_next_frame` return. Each row contains the
controller bytes that ran that frame, a named RAM subset (hex bytes, all four
riders where applicable), all 256 OAM bytes, the last scroll writes and actual
PPU `v`, `t`, fine-x and latch fields, and 32 palette bytes. Optional pixels
are 256x240 row-major NES palette indices, as hex bytes. `$0080` is screen x;
`$0088` is drawing order, and `$00A8` is the active-object flag. Pose is
computed by the cartridge's `sub_E1F9` and represented by its emitted OAM;
it has no raw RAM pose byte.

A verified one-line CPU-write hook is inserted into a generated copy of
agnes under `build/`. It captures `$2005` writes and optional APU writes to
`$4000-$4017`, excluding DMA and controller strobe. Cycle offsets have
instruction granularity. agnes does not emulate audio: this is the raw write
log, used to inspect the cartridge's sound driver. PPU scroll writes retain
the actual pre-write latch, including resets caused by `$2002` reads.

The manifest records pins, script digest, RAM field addresses, frame count,
pixel frames and compressed-file hashes. Gzip timestamps and embedded
filenames are omitted for repeatable refreshes. No ROM, PRG or CHR binary is
committed. Required regression rows consume fixtures with a second reader
in `tests/excitebike_oracle.py`; they run without this tool, the disassembly,
a compiler, or network access.
