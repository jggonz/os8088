# The three Commodore ROM images

These three files are **Copyright © Commodore Business Machines**. They are
neither GPL nor ours, and they are distributed here exactly as VICE 3.10
distributes them in its own `data/C64/` directory.

| file | bytes | SHA-256 |
|---|---|---|
| `kernal-901227-03.bin` | 8,192 | `83c60d47047d7beab8e5b7bf6f67f80daa088b7a6a27de0d7e016f6484042721` |
| `basic-901226-01.bin` | 8,192 | `89878cea0a268734696de11c4bae593eaaa506465d2029d619c0e0cbccdfa62d` |
| `chargen-901225-01.bin` | 4,096 | `fd0d53b8480e86163ac98998976c72cc58d5dd8eb824ed7b829774e74213b420` |

They are **the C64 defaults VICE 3.10 itself picks**, not a guess:
`src/c64/c64-resources.c:378` sets `KernalName` to `C64_KERNAL_REV3_NAME`
(`kernal_revision = C64_KERNAL_REV3`, `:74`), `:381` sets `BasicName` to
`C64_BASIC_NAME` and `:375` `ChargenName` to `C64_CHARGEN_NAME` — the three
names spelled out in `src/c64/c64rom.h:52`, `:31` and `:60`.

## Why they are committed, when nothing else third-party is

`CONTRIBUTING.md` §6 says nothing third-party is committed. **This is a
stated, user-decided departure from that rule, for these three files only**,
recorded in `docs/C64-SPEC.md` §1.3 and `docs/C64-PORT-PLAN.md`'s Decision 1.
The user's words were: *"move all the necessary code into my repo, and use a
sidecar that is loaded at runtime. Don't cut features, make it modular so that
we can load the rom at runtime instead of embedding it in the package."*

Two things follow, and both are the point:

- **The build needs no VICE checkout and no network.** `tools/c64rom.py`
  concatenates these three into `build/c64-rom/C64.ROM` on any clone.
- **They are not embedded in the package.** `C64.ROM` is a 20,480-byte
  sidecar file that ships beside `C64.O88` and `C64.OVL` and is read into a
  heap claim of its own at launch (`docs/C64-SPEC.md` §1.4). Embedded, the
  package would have been about 73,000 bytes against SPEC.md §73's 61,440
  cap — refused on paper, which is what made the sidecar the design.

A disk without `C64.ROM` is a program that refuses at launch naming the file.
