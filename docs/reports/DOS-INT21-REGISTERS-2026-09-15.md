# What `INT 21h` gives back — os8088's DOS box against IBM DOS 3.30

**A measurement, not a description.** Taken 2026-09-15 on a four-core cloud
container, `nasm` 2.16.01, MartyPC at its pinned commit, at `2d08d51` on
`claude/dos-arena-home`. Both columns come from **one binary**,
`tests/dostrap/regs.asm`, run twice:

| | |
|---|---|
| ours | `build/os8088-360.img` in A:, `build/dosregs360.img` in B:, machine `os8088_5150_herc_gla`, `REGS.COM` double-clicked from the Disk window |
| the reference | **IBM Personal Computer DOS 3.30** (`Ibm330_1.img`, md5 `12dd70c6…`) in A:, the same `build/dosregs360.img` in B:, machine `os8088_5150_herc_sb_720_gla`, `B:` then `REGS` typed at `A>` |

The DOS floppy is the fork owner's, supplied for development and **not in this
repository**. The reference machine is GLaBIOS because the IBM ROM is not in
the tree either (docs/MARTYPC-DEBUG.md); the BIOS is not under test here — no
row in this table reaches one.

It is true of that commit and of no other tree; a later measurement is a new
file. `tests/dosregs.py` is the gate that keeps the answer, and
SPEC.md 96.7.1.2 is what was concluded from it.

## The answer in four lines

1. **44 of the 45 calls, and all four device-word readings, now agree to the
   character.** The one that does not is `AH=57h`, which this box does not
   implement.
2. **Going in, the open question was `BX` and `CX`, and they were very nearly
   clean**: of 45 calls exactly one gave a register back that DOS preserves —
   `AH=47h`, which spent `CX` on a buffer length and never put it back.
3. **`AH=44h`'s device word was wrong twice**, once recorded (SPEC.md
   96.7.1.1's third row, bit 6) and once nobody had looked for: bits 0..5 were
   the drive the *program* was standing on, where DOS answers the drive the
   *file* is on.
4. **The per-handler discipline SPEC.md 96.7.1 distrusted is, measured, mostly
   holding** — which is what decided the fix for finding 1 against banking
   `BX` and `CX` at the gate.

## How to read the table

Every register the call does not need goes in carrying a sentinel, the ones it
does need carry real arguments, and the whole set is pushed *the instruction
after the `int`* — before the `AH=02h` that prints it, which is itself one of
the calls under test. `.` means the register came back; a letter means it did
not.

    B C D S I P E G   =  BX CX DX SI DI BP ES DS

The trailing digit is `CF`. **A letter is not a defect**: five functions
answer in `DX`, `AH=30h` in `BX` and `CX`, `AH=2Fh` and `AH=35h` in `ES:BX`,
`AH=43h` in `CX`, `AH=36h` in all three, `AH=29h` advances `SI`. The finding
is the **diff between the columns**, and the probe judges nothing.

Labels: `44o` / `44c` / `44w` / `44x` are four readings of `AH=44h AL=00h` —
just opened, just created, after a write, and on a file on another drive.
`3Ea`/`3Eb`/`3Ex` are three closes, `47`/`47b` the same call from the root and
from a subdirectory, `35`/`35b` `INT 33h` and the vector `AH=25h` had just
set, `0Ea`/`0Eb` the drive switch out and back.

## The table, BEFORE the fixes

| | ours (`2d08d51`) | IBM DOS 3.30 | |
|---|---|---|---|
| `19 ` | `......../0` | `......../0` | |
| `2A ` | `.CD...../0` | `.CD...../0` | `CX`, `DX` are the date |
| `2C ` | `.CD...../0` | `.CD...../0` | `CX`, `DX` are the time |
| `30 ` | `BC....../0` | `BC....../0` | `BX`, `CX` are the OEM serial |
| `2F ` | `B.....E./0` | `B.....E./0` | `ES:BX` is the DTA |
| `1A ` | `......../0` | `......../0` | |
| `35 ` | `B.....E./0` | `B.....E./0` | `ES:BX` is the vector |
| **`47 `** | **`.C....../0`** | **`......../0`** | **finding 1** |
| `3D ` | `......../0` | `......../0` | `DX` preserved since 96.7.1.1 |
| `44o` | `..D...../0` | `..D...../0` | `DX` is the device word |
| `3F ` | `......../0` | `......../0` | |
| `42 ` | `......../0` | `......../0` | `DX:AX` is the position |
| **`57 `** | **`......../1`** | **`.CD...../0`** | **finding 3** |
| `3Ea` | `......../0` | `......../0` | |
| `43 ` | `.C....../0` | `.C....../0` | `CX` is the attribute |
| `4E ` | `......../0` | `......../0` | |
| `4F ` | `......../1` | `......../1` | ran out — one file on the disk |
| `36 ` | `BCD...../0` | `BCD...../0` | all three are the free space |
| `3C ` | `......../0` | `......../0` | |
| `44c` | `..D...../0` | `..D...../0` | |
| `40 ` | `......../0` | `......../0` | |
| `44w` | `..D...../0` | `..D...../0` | |
| `3Eb` | `......../0` | `......../0` | |
| `56 ` | `......../0` | `......../0` | |
| `02 ` | `......../0` | `......../0` | |
| `0B ` | `......../0` | `......../0` | |
| `06 ` | `......../0` | `......../0` | |
| `09 ` | `......../0` | `......../0` | |
| `0C ` | `......../0` | `......../0` | |
| `0E ` | `......../0` | `......../0` | |
| `29 ` | `...S..../0` | `...S..../0` | `SI` is advanced past the name |
| `25 ` | `......../0` | `......../0` | |
| `35b` | `B.....E./0` | `B.....E./0` | |
| `39 ` | `......../0` | `......../0` | |
| `3B ` | `......../0` | `......../0` | |
| **`47b`** | **`.C....../0`** | **`......../0`** | **finding 1, from a subdirectory** |
| `3Bb` | `......../0` | `......../0` | |
| `3A ` | `......../0` | `......../0` | |
| `4D ` | `......../0` | `......../0` | |
| `41 ` | `......../0` | `......../0` | |
| `0Ea` | `......../0` | `......../0` | |
| `3Dx` | `......../0` | `......../0` | |
| `44x` | `..D...../0` | `..D...../0` | |
| `3Ex` | `......../0` | `......../0` | |
| `0Eb` | `......../0` | `......../0` | |

...and the device word, which the mask can only say CHANGED:

| `AH=44h AL=00h` on | ours (`2d08d51`) | IBM DOS 3.30 |
|---|---|---|
| a freshly OPENED file on B: | `0001` | **`0041`** |
| a freshly CREATED file on B: | `0001` | **`0041`** |
| ...the same handle after `AH=40h` | `0001` | `0001` |
| a file on B:, with the machine on A: | `0000` | **`0041`** |

## What is NOT in the table, and why

Four functions the box dispatches cannot be asked this way, and one group is
left out on purpose:

- **`AH=4Bh`** needs a child program; `dosexec` is its gate.
- **`AH=01h`, `07h`, `08h`** block on a keystroke. A probe that blocks is a
  probe that hangs the harness.
- **`AH=4Ch`, `00h`** do not return.
- **`AH=48h`, `49h`, `4Ah`.** A `.COM` owns all of memory under a real DOS, so
  `AH=48h` fails there for a reason that has nothing to do with registers.
  What that comparison measures is the two memory models; `dosmem` and
  `dosarena` are its gates.

## Finding 1 — `AH=47h` eats `CX`

`.getcwd` sets `CX = DOS_PBUF` for `dos_be_path`'s buffer length and never
restores it, so a program that kept a count in `CX` across *"where am I
standing"* got **132** back. Both rows catch it, from the root and from a
subdirectory. Fixed with two instructions in the handler rather than by
banking `CX` at the gate — SPEC.md 96.7.1.2 carries that argument, and the
number it rests on is in this file: **one handler in 45**.

## Finding 2 — the device word, twice

Bit 6 is *"this handle has NOT been written through"*, and this box had no
per-handle flag to answer it from — which is exactly what SPEC.md 96.7.1.1
recorded rather than guessing at. `FH_FLAGS` had a spare bit. The three
readings are what a single one could not have distinguished: **`0041`
opened, `0041` created, `0001` written.**

The drive bits are a second defect in the same word and **no same-drive test
could have shown it**: the handler read `[dos_vol]`, where the program is
standing, and DOS answers the drive the file is on. They are the same number
until a program opens `B:NAME` from A:, which is why the probe's last five
rows do precisely that — and IBM DOS 3.30 answers `0041` with the machine on
A:, the file's drive and not the program's.

## Finding 3 — `AH=57h` is not implemented

DOS answers `CX` = time, `DX` = date for an open handle. This box falls to the
invalid-function arm: `CF=1`, `AX=0001h`. **Not fixed here, and the reason is
a fact about a different layer**: `OSAPI_FILE_FIND`'s 24-byte record
(SPEC.md 19.7.1) carries no timestamp, so answering `AH=57h` is a published
kernel ABI change rather than a DOS-box change. The row is kept in
`tests/dosregs.py` as a KNOWN gap, so that implementing it fails that gate and
forces the expectation to be updated — the opposite of a gap nobody holds.

## The cost of the fixes

`doscore.bin` **14,632 → 14,653**, +21 bytes, against `CORE_MAX` 14,848 —
195 left. `dosp.bin` and `kerndos.bin` are **byte-identical**, the three
handlers all being core code, and `DOS.O88` is unchanged at 44,073 (the
compressed part absorbed it). No kernel section moved at all.

## Reproducing it

```sh
make build/dosregs360.img
python3 tools/os88test.py soak -k dosregs --user-asked     # our column, asserted
```

The reference column needs a DOS floppy of your own:

```sh
python3 - <<'PY'
import sys, time; sys.path.insert(0, "tools"); import os88marty
with os88marty.launch("YOURDOS.img", apps="build/dosregs360.img",
                      machine="os8088_5150_herc_sb_720_gla", boot=30) as m:
    for _ in range(2): m.key("Enter"); time.sleep(3)   # date and time prompts
    m.type_text("B:"); m.key("Enter"); time.sleep(3)
    m.type_text("REGS"); m.key("Enter"); time.sleep(20)
    print("\n".join(r.rstrip() for r in (m.screen() or []) if r.strip()))
PY
```

`os88marty.launch` clones both images, so neither floppy is written — verified
by `md5sum` across a run that creates, writes, renames and deletes a file.
