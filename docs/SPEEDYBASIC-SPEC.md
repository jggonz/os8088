# SPEEDYBA / SPEEDY BASIC — the Turbo Basic interpreter

This document is the binding contract for `apps/speedybasic/`. The reference
for language behavior and the demonstration corpus is the sibling
`../speedybasic` web IDE. The os8088 port changes the host services and user
interface; it keeps the tokenizer, parser, runner, numeric and string behavior,
control flow, text output, graphics, sound, and system emulation needed by all
of that project's demonstrations.

## 1. Application shape

SPEEDY BASIC is an ordinary windowed os8088 C package, `SPEEDYBA.O88`, with
the required `SPEEDYBA.OVL` beside it. It owns one document window with an editor and a runtime view. Run starts the current
source, Stop interrupts it, and Reset restores the runtime without closing the
editor. Switching from Output to Editor stops a running program before its
source can be edited or replaced. Closing the application tears down any runner task and releases every
claim it owns.

The runtime draws inside the window by default. Full Screen uses the ordinary
window fullscreen latch (§11.2): entering it preserves the editor and runner state, and
leaving it returns to the same window and repaints the runtime view. A program
switching between text and graphics modes changes the runtime surface, never
the desktop video mode.

The primary image plus BSS remains below `APP_MAX_SIZE` (§3). Cold editor,
file, menu and complex graphics helpers live in `SPEEDYBA.OVL`; the resident
callbacks and runner remain in the package. The overlay is loaded before the
first paint, so paint never starts file I/O. Raising the segment limit is not
an option.

## 2. BASIC files and associations

The editor reads and writes ASCII `.BAS` files through the standard file API.
The application declares **no `.BAS` association**. APPLE2 already declares
that suffix on the everything disk and live media; two declarations would make
directory order choose the winner (§54.5), the same collision avoided by the
precedent in §95.2. A Speedy BASIC program is opened from the application's
File menu. This rule applies to the dedicated disk too, so the same source has
the same launch path on every volume.

Low-level Turbo Basic operations use the interpreter's emulated 8086 memory,
ports, BIOS data and video memory. They do not expose the package's real
segment or the os8088 kernel. File commands remain inside the instance's
current volume and directory.

## 3. Demonstration corpus

The disk carries all 29 samples from
`../speedybasic/src/features/samples/samples.ts` as ordinary `.BAS` files:

`VRAMPOKE`, `VRAMRAIN`, `LIFE`, `LIFEHD`, `HELLO`, `COLORS`, `LOCATE`,
`BOING`, `PATTERN`, `CODEDIFF`, `MINICALC`, `WORD`, `WORDXT`, `LANDER`,
`LANDERHD`, `PIANO`, `TANKS`, `CUBEPOKE`, `CUBEDRAW`, `CUBEPOKEHD`,
`CUBEFAST`, `CUBEHDF`, `CUBEASM`, `FIRE`, `PLASMA`, `MANDEL`, `SNAKE`,
`COPPER`, and `STARS3D`.

`CUBEPOKEHD.BAS` is `CUBEPKHD.BAS` on disk because FAT has an eight-character
stem limit. Its source bytes are unchanged.

The canonical extracted files are committed under
`apps/speedybasic/demos/`. `tools/speedybasic_samples.py` reads
`MANIFEST.TXT`, verifies the committed bytes against the sibling TypeScript
when that checkout exists, and materializes the corpus in
`build/speedybasic-demos/`. A clone without the sibling uses the committed
copy. It was extracted from `github.com/jggonz/speedybasic` commit
`1dc7d4d02d3ffb9f8097e0a5491b616416c6c540`. The ordered corpus is 189,898 bytes with SHA-256
`eeff10fd8fcf1c4213e105105edc049cc8a4964a98f04f2c10d671ad3af66139`.

## 4. Disk layout and capacity

`make speedybasicdisk` builds all four standard application geometries:

| image | geometry |
|---|---|
| `build/speedybasic.img` | 1.44MB |
| `build/speedybasic120.img` | 1.2MB 5.25-inch HD |
| `build/speedybasic720.img` | 720KB |
| `build/speedybasic360.img` | 360KB |

Each has `SPEEDY/SPEEDYBA.O88`, `SPEEDY/SPEEDYBA.OVL`, `SPEEDY/README.TXT`, and the 29 programs in
`SPEEDY/DEMOS/`. `SYSTEM/APPDATA/` exists for application state. DEMOS is
allocated with 64 directory slots so edited programs can be saved; the kernel
does not grow FAT directories (§18.5). The smallest disk's 354 1KB clusters
are the binding capacity check, and every image recipe runs the independent
structural verifier after construction.

SPEEDY BASIC stays out of the default software disks: it requires the C
toolchain and its complete source corpus is too large for the curated 360KB
apps disk. It does join `make allapps` and the live media. The 1.44MB
everything disk and live media carry the package, required overlay, guide and
demos in the same two-folder layout. The 1.2MB everything disk carries the
package and overlay; its complete demo corpus remains on
`speedybasic120.img`, since the additional 190KB does not fit beside every
other application. The everything-disk builder prices the 64-slot demo
directory where present and reduces RunCPM's ranked software fill to fit each
geometry.

## 5. Verification

The host extractor gate checks the manifest count, 8.3 names, ASCII source,
ordered digest, sibling identity when available, and sibling-free generation.
The package's host tests exercise the same parser and runner sources compiled
with host-service stubs. The MartyPC application test opens and runs text and
graphics samples, enters and leaves fullscreen, checks repaint, stops the
runner, and confirms teardown. The complete demo sweep must run every name in
`MANIFEST.TXT`; one graphical smoke is not evidence that the corpus works.
