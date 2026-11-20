# Boratype developer notes

Build from the normal os8088 checkout with Python3.11+, NASM and the pinned
SmallerC toolchain described in docs/C-TOOLCHAIN.md:

```sh
make deps
make boratype
make boratypedisk boratypebundle
make run RUNAPPS=build/boratype.img
```

Open BORATYPE.O88 from the software disk. The360KB image is
`build/boratype360.img`;720KB,1.2MB and1.44MB counterparts are built together.
`build/BORATYPE.WPK` is produced by the existing os88wire archive tool. The
base distribution includes every lesson, original caption, score, effect,
font and portrait. Recorded voice clips were removed from scope by the owner.
The disk pre-creates SYSTEM/APPDATA for installed-volume progress.

## Architecture and real reference paths

`apps/boratype/boratype.c` is one readable C translation unit. The core and
codec know no OS callbacks. `bt_platform.c` owns native commands, timing,
window/focus/fullscreen state and file boundaries. `bt_face.c` reads state;
it never chooses reactions, advances a lesson or consumes random numbers.
`bt_audio.c` consumes events and the actual guest tick.

The concrete reference map and observations are in
[the build diary](../HOW-BORATYPE-WAS-BUILT.md). Particularly useful sources:
`apps/breadxt/face.c`/`draw.inc`, `apps/byteopoly/bp_face.c`/`bp_app.c`,
`apps/cc/gk.inc`, `gkmus.inc` and `crt0.inc`. These are shared source,
not copied mechanisms. The foreign CGA320renderer is private only within the
existing FSX bracket, whose returned geometry/bank/segment it checks.

## Resource ownership

The runtime owns the movable resident region and eager art/overlay parts.
The UI allocates an8KB movable opaque drawing band. Music and sound effects
use the existing OS tone/FM leases; there are no PCM buffers, audio file
imports or stream grants. Files are
read or saved only on the UI task outside paint/input/audio callbacks.
The timer fallback worker only posts wakes and is restartable. Close cancels
timers, stops/releases audio, saves validated progress and returns through
the OS fullscreen mechanism. Another instance has read-only progress and
does not acquire sound.

Cold startup, file/codec, menu/body and non-typing menu-key work use the
existing eager C overlay. Active scoring and input remain resident. Geometry
arrangement is cold; unchanged-layout checks are hot. No OS resident budget,
ABI number or API slot was changed for this package.

## Formats and content sources

The binding [BORATYPE-SPEC](../BORATYPE-SPEC.md) pins the2136-byte BTT1 store.
Alternating generations keep the previous valid save intact until a new
write has been read back and verified. [Engine notes](engine.md) describe
strict input, arithmetic, sample thresholds, timing and profile attribution.
[Audio notes](audio.md) document the composed scores, effects and OS routing.
The BTT v1 voice byte is retained as an unused legacy field; it defaults off
and cannot enable a clip path. [Face notes](face.md) cover native
geometry and asset IDs.

`tools/os88btart.py build` and `build-sound` use the existing sprite/font
packers and score compiler on committed pixel/text sources. `draft` and
`draft-font` are optional development operations. They never run in make.
The24px face is selected exact-pixel source derived from the committed
Wine dialog font, retaining its LGPL provenance.

## Measurement boundary

The pre-repair installed package's resident image was53,220bytes plus
10,071BSS (63,291total), with a9,241-byte eager overlay. Its art banks were
63,677 and12,823bytes; compressed package83,118bytes. These are intermediate
measurements, not the final ledger. The kernel resident footprint did not
change. Final package/claim costs and per-adapter results belong in the
evidence report after the repair and acceptance runs.

MartyPC at4.77MHz measured an unchanged pre-repair UI pulse at408,505–415,879
cycles (85.6–87.2ms), and a portrait blink at about1.66seconds. The failure
was recorded before changing the face. Initial cache fixes reduced face-only
work from338,162cycles/70.85ms to29,008/6.08ms. Cosmetic slicing and actual
key-to-glass latency still require final measurements; QEMU cannot prove them.

Technical guest captures established FM and speaker activity, muting, route
changes and cleanup. They do not establish subjective musical quality. The
audio forwarding tool explicitly refused audio input in this session, so
listening remains unverified. Voice clips and their importer were removed
from the product when the owner chose sound effects and visual messages.
