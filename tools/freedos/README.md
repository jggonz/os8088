# Building FreeDOS on macOS

`tools/build-freedos.sh` builds the FreeDOS payload for the DOS floppy —
`KERNEL.SYS`, `COMMAND.COM` and a CHS-only boot sector — from the sibling
`../kernel` and `../freecom` checkouts. This file is why it needs the two
modifications it makes, because both of them are silent when they are wrong and
neither is guessable from the failure.

Run it with `make dos`, never as part of `make all`: the first run fetches a
~148MB toolchain, and a default target that needs the network is a default
target that fails on a train.

## The host toolchain

Open Watcom V2's rolling snapshot ships **native Apple-Silicon host binaries**
in `armo64/` — `wcc` is a `Mach-O 64-bit executable arm64`. No Docker, no
Rosetta, no `ia16-elf-gcc`, no `upx`. This is the thing most likely to be
assumed impossible: FreeDOS's own CI builds on Ubuntu and uses `binl64`, so
every account of "how to build FreeDOS" points at Linux.

`$WATCOM` set in the environment wins, so a developer with their own install
pays none of the download.

The snapshot URL is a **rolling tag** — the same URL serves different bits over
time. `OW_SHA` in the script records the build this was proven against. A
mismatch warns rather than fails, because hard-failing would break the build
every time upstream ships; if FreeDOS starts misbehaving after a toolchain
refresh, that warning is the first suspect.

## Modification 1 — `patches/owosx.mak`

A new compiler profile, copied into the kernel copy's `mkfiles/` and selected
with `COMPILER=owosx`. It is `owlinux.mak` plus two changes.

**`NASMFLAGS=$(NASMFLAGS) -Dowlinux` is load-bearing.** `mkfiles/generic.mak`
passes only `-D$(COMPILER)` to nasm, while `kernel/segs.inc` and
`kernel/asmsupt.asm` test `%ifdef owlinux` to select the Watcom segment layout.
A profile named anything else arrives without that define, the assembler
quietly takes the non-Watcom `TGROUP`/`IGROUP` path, and **the kernel still
links** — it is simply the wrong kernel. There is no error to read.

**`CLT`/`CLC` are clang** because Open Watcom has a Darwin *host* build but no
Darwin *code generator*. The build's own helper programs (`utils/exeflat.exe`,
`utils/patchobj.com`) run on this machine, so this machine's compiler builds
them. `-std=gnu89` because the sources predate C99 declarations.

## Modification 2 — dropping `-DGCC` from FreeCOM

`freecom/mkfiles/watcom.mak`'s `__OSX__` branch builds the host utilities with
clang and passes `-DGCC` while doing it. **This is an upstream bug**, and worth
reporting.

`-DGCC` does not mean "the host compiler is gcc". It means "the *target* was
built by ia16-gcc": `tools/ptchsize.c` takes the ia16 branch under it and
rewrites the MZ header's `fSP`. Applied to a Watcom-linked `command.com` that
is simply wrong — the image comes out with `sp=0x3010` and
`minalloc=maxalloc=768` instead of `sp=0x1000`.

FreeDOS then rejects it with

```
Bad or missing Command Interpreter: B:\COMMAND.COM
```

which reads like a missing or misnamed file and is nothing of the kind. The
file is present and is a valid MZ image; it just has an unloadable stack.
`kernel/kernel/task.c` notes that this one message covers both "failed to load"
and "loaded and exited immediately", which is why it never points at the
header. Expect to lose an afternoon to this if it regresses.

The edit is a `sed`, not a `.patch`, because these files are **CRLF** and a
patch context written on this side of the fence never matches — the hunk
applies cleanly by eye and is rejected in fact. `sed` does not care about line
endings, but it also does not report a miss, so the script asserts `-DGCC` is
gone afterwards and fails loudly if it is not.

## Why these are applied to copies

`build-freedos.sh` copies `../kernel` and `../freecom` into `build/freedos/`
and patches the copies. Those trees are somebody else's repository, and a
patched working tree there is a trap for whoever next types `git pull` in them.

It also means both modifications must survive a resync of the vendored trees.
If either stops applying, the assertions above are what will say so.

## Build inputs that are pinned

| input | pin | why |
|---|---|---|
| Open Watcom snapshot | `OW_SHA` (warn only) | rolling tag |
| `FDOS/country` | `COUNTRY_REV` | `kernel/kernel/config.c` `#include`s `../country/kernel.tb1` outright, so an unpopulated submodule is a compile error |
| CPU target | `XCPU=86` | the target is an 8088, not the 386 CI defaults to |
| FAT width | `XFAT=16` | nothing here has a FAT32 volume; keeps the kernel small |
| compression | `XUPX=` (empty) | `upx` is not installed, and 70KB uncompressed still fits the 128KB the boot sector can load |

The kernel comes out as `bin/kwc<XCPU><XFAT>.sys` — `kwc8616.sys` — **not**
`kernl086.sys`, which is the name the ibiblio *release* uses.
