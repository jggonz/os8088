# The LEMMINGS converted band format (wave 1 pin)

Pinned by the wave-1 implementer so `tools/os88lem.py` and `apps/lemmings/`
can be written at the same time. It is the concrete form of SPEC.md §116.3 and
§116.3.2 and nothing here may contradict those two sections.

Everything is **little-endian**. Every file is **padded to a multiple of 512**
and **no file exceeds 64,512 bytes** (§116.3.2). Every name is uppercase 8.3 in
`[A-Z0-9_-]`.

## The files

| name | holds | read by |
|---|---|---|
| `LEMMAN.LEM` | the manifest header: what this disk carries, and the cluster arithmetic it was built with | wave 1 |
| `LEMR0.LEM` … `LEMR3.LEM` | one file per RATING: 30 level entries of 64 bytes with the preview fields already unpacked | wave 1 |
| `LEMSTR.LEM` | the string resource band (§116.3, "the string resource band is written here too") | wave 1 |
| `LEMLV0.LEM` … `LEMLV9.LEM` | the 80 raw 2048-byte level records, **eight to a file** (§116.3.2), ODDTABLE already folded in | wave 4 |
| `LEMGR0.LEM` … `LEMGR4.LEM` | one style bank per graphic set: palette, terrain pieces, object metadata and frames. Header magic `LGRB`, **version 2** since wave 3, which added each object slot's GRND animation flags at byte 896 and its preview frame at 912 (the layout is `tools/os88lem.py`'s STYLE BANK block); the package refuses version 1 rather than reading zeros as "never animates" | wave 2, v2 in wave 3 |
| `LEMMAIN.LEM` | the MAIN.DAT-derived bank: panel bitmap, both fonts, the 28 animations, the masks, the rating signs, the brown background. Header magic `LMNB`, **version 2** since wave 5, which added the MAIN-MENU palette at byte 288 (16 entries of three 6-bit components, the format document's own table - MAIN.DAT does not carry it); the preview and postview screens are drawn in it and the package refuses version 1 rather than lettering them in black. **Version 3** since wave 6 adds item 10, the explosion's spray (decision 5, SPEC.md 116.8): `PARTICLES.DAT` byte for byte - 51 rows of 80 signed (dx, dy) pairs, a pair with either byte -128 not drawn - then 51 rows of four signed bytes, each frame's dx min, dx max, dy min, dy max over the particles drawn, or (127, -128, 127, -128) for none; 8,364 bytes, which takes the bank from 52,224 to 60,416 bytes and its claim from 52 KB to 60 | wave 2, v2 in wave 5, v3 in wave 6 |
| `LEMSP0_0.LEM` … | the four VGASPEC pictures, in numbered parts (`LEMSP<n>_<part>.LEM`) because one is 76,800 bytes. Header magic `LSPB`, **version 2** since wave 4: the 512-byte header (8 total length, 12 width 960, 14 height 160, 16 bpp 4, 17 piece columns, 18 piece rows, 20 piece width 480, 22 piece height 40, 24 x 304; 32 the custom palette with entry 0 Lemmix's hardcoded 31, 31, 0, 56 the level's ground set's standard palette - the style bank's own two offsets) and then the picture as 2 x 4 PIECES, row-major, each laid out as a terrain piece is (planes 0-3, one plane stride apart), so `lem_r_wpiece` composes it. Version 1 was four whole planes | wave 4 |
| `LEVELS.TXT` | plain text: what this disk carries and why the rest is not on it | the reader |

## `LEMMAN.LEM`

**ONE FILE PER RATING, and that is not a layout preference.** A single index
with the four ratings inside it would have to be read a rating at a time —
1,920 bytes of a 10 KB file — and the only slot that reads part of a file is
`os88_file_read_at()`, whose offset and cap must each be **a whole number of
clusters** (`apps/cc/os88.h`). The cluster is 512 bytes on the 1.44MB and
1.2MB geometries and 1,024 on the 720KB and 360KB ones, and it is neither of
those on a RAM disk — which is exactly where The Wire unpacks this package
(§116.3.2). So the rating is the FILE: `os88_file_read()` reads a whole small
file into a DS-relative static with no alignment rule of any kind, and the
package carries one rating's 2,048 bytes of bss rather than four ratings'
8,192. Four extra directory rows against `RD_MAXENT`'s 96 is the price and
there is room.

`LEMMAN.LEM`'s header, 512 bytes:

```
  0   8   magic     "OS88LEM" + NUL
  8   2   version   1
 10   2   nlevels   how many level entries follow (<= 120)
 12   2   lesize    bytes per level entry = 64
 14   2   leoff     byte offset of the first entry in a RATING FILE = 0
 16   1   styles    bitmask, bit n set = LEMGRn.LEM is on this disk
 17   1   specials  bitmask, bit n set = the parts of special n are on this disk
 18   2   setbytes  bytes of converted data on this disk
 20   2   setclus   clusters the BUILT image uses: the bands, LEMMINGS.O88,
                    README.TXT, the folders and ASSOC.DAT - exactly what
                    os88disk.py --verify counts (os88lem.py --check-image
                    refuses otherwise); LEMMINGS.SAV's two clusters are not in it
 22   8   geom      "1.44MB" / "720KB" / "360KB" / "1.2MB", NUL-padded - the OS's
                    own spelling, which LEVELS.TXT uses too (wave 5; it was
                    "1440K" / "720K" / "360K" / "1200K")
 30   2   strbytes  the length of LEMSTR.LEM's payload (before its 512 pad)
 32   2   nstyles   how many style banks were converted at all (5)
 34   2   nparts0..3 (4 words) parts per special picture, 0 = not on this disk
 42 470   reserved, zero
```

## `LEMR0.LEM` … `LEMR3.LEM`

30 level entries of 64 bytes from offset 0 — 1,920 bytes — padded to 2,048.
A rating that this disk carries no levels of is still written, with every
entry's `flags` bit 0 clear and its `missing` naming why.

Level entry, 64 bytes:

```
  0   1   rating    0 Fun, 1 Tricky, 2 Taxing, 3 Mayhem
  1   1   number    0..29 within the rating
  2   1   style     0..4 graphic set
  3   1   special   the VGASPEC index, or 0xFF
  4   2   rate      release rate
  6   2   count     number of lemmings
  8   2   save      number to be saved
 10   2   minutes   time limit in minutes
 12   8   skills    the eight skill counts, in the panel's order
 20   2   startx    the level's start scroll x
 22   1   rawfile   which LEMLVn.LEM holds the record (0..9)
 23   1   rawsect   which of that file's eight records (0..7)
 24   1   flags     bit 0 = playable on THIS disk
 25   1   missing   a string id naming what is missing, 0 when flags bit 0 is 1
 26   2   oddtable  1 = this level took its header and name from ODDTABLE.DAT
 28   1   flags2    bit 0 = SUPERLEMMING (LVL 0x001E-0x001F = 0xFFFF; Lemmix
                    Level.Loader.pas:104). No level of the 120 sets it.
 29   3   reserved, zero
 32  32   name      the original's 32-byte name, trailing spaces stripped, NUL
                    padded
```

## `LEMSTR.LEM`

```
  0   4   magic     "LSTR"
  4   2   count
  6   2*count  offsets, each from the START OF THE FILE, of a NUL-terminated
               string
 ...      the strings, in id order
```

Padded to 512. The package reads the whole band into bss once and `lem_str(id)`
returns a pointer into it, so a string costs no disk and no copy.

**It is 4,096 bytes today, not the ~2 KB this paragraph first estimated** — the
verbatim greying facts are the bulk of it (`tools/os88lem.py`'s table), and
4,089 bytes of table pad to 4,096. The package's buffer is **5,120**, which is
the number `LEM_STRBUF` carries in `apps/lemmings/lemmings.c`; a band that
grows past that is a refusal at `lem_str_load()` and not a silent truncation.
It was 4,096, and wave 1's own two per-adapter Mode facts and the Psygnosis
copyright took the band from 3,584 to exactly that — the very step that would
have refused. The next 512-step is 4,608, so 5,120 clears the band by a whole
step, which is what `lem_str_load()`'s strict `n >= LEM_STRBUF` needs; the
8,192 wave 1 first took was 3,072 bytes of headroom charged to a bss line that
had none to give (`apps/lemmings/lemmings.c`, §116.6).

**The ids are generated.** `tools/os88lem.py` writes `build/lemstr.h` beside
the band — `#define LEMS_<NAME> <n>` for every string — and `apps/lemmings`
`#include`s it, so the two halves cannot drift: a renamed string is a compile
error, not a wrong sentence on the glass.

The string table's source is a table inside `tools/os88lem.py`, one row per
string, each row carrying **the reference file that defines it** in a comment
(SPEC.md §116's authority table).

## `--fixture`

`python3 tools/os88lem.py --fixture apps/lemmings/hosttest/fixture` writes a
tiny SYNTHETIC set — `LEMMAN.LEM`, a rating file with **three playable levels
and one greyed row**, `LEMLV0.LEM` with their records, `LEMSTR.LEM` with the
whole real string table (the strings are the port's own text and the
reference's verbatim on-screen wording, not decoded data), one style bank stub
with four invented terrain pieces and five object types (exit, hatch, water,
a trap, fire), and a `LEMMAIN.LEM` stub with synthetic lemming animations,
destruction stencils and an INVENTED spray table (item 10: arcs of the
fixture's own, never the original's trajectories) — carrying **no bytes derived from the original data
files**, so it can be committed and the host harness runs with no fetch. The
three playable levels are what the mechanics replay drives (SPEC.md 116.12,
`apps/lemmings/hosttest/mech/`): a change to them moves the replay's committed
trace hashes, and `apps/lemmings/build.sh` says so by name.
