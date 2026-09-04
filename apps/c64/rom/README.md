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
- **They are still LOADED AT RUNTIME, and they are no longer a separate
  file.** They are a PART of `C64.O88` (SPEC.md §20.12, `docs/C64-SPEC.md`
  section 1.4): 20,480 bytes appended past the program's image, claimed and read into
  a heap claim of their own at launch, by package code, before the C runs.
  Nothing is linked into the image and nothing is resident that was not
  resident before — what changed is that the bytes travel in the same file
  instead of beside it.

**THE SIDECAR WAS A USER DECISION AND ITS REASON WAS A WRONG NUMBER.** The
quote above asked for a runtime-loaded ROM *"instead of embedding it in the
package"*, and this section used to record why the alternative never got a
hearing: embedded, "the package would have been about 73,000 bytes against
SPEC.md §73's 61,440 cap — refused on paper". `APP_MAX_SIZE` bounds the
primary SEGMENT's image plus bss, not the FILE. Measured after the conversion:
image **40,854**, bss **13,176**, sum **54,030** against that same 61,440 cap,
in a file of **61,440** bytes. The cap was never the obstacle — until
`docs/O88-MULTISEG-PLAN.md` there was simply nowhere in the format to put
bytes that are not the image.

The runtime-loading half of the decision stands and is what the parts standard
does. The separate-file half is what this conversion reverses, and it is one
`%define` in `apps/c64/c64.asm` plus one Makefile argument if it should ever
go back.
