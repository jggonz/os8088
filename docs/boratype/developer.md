# Boratype developer notes

Build from the normal os8088 checkout with Python3.11+, NASM and the pinned
SmallerC toolchain described in docs/C-TOOLCHAIN.md:

```sh
make deps
make boratype
make boratypedisk boratypebundle
make run RUNAPPS=build/boratype.img
```

Open BORATYPE.O88 from the software disk. The360 KB image is
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

## Measurements and acceptance

The [acceptance report](EVIDENCE.md) is the final ledger: package and claim
sizes, cycle measurements, installed-app scenarios, adapter evidence and
specific limits. It links the captured guest gallery and its hash manifest.
The binding contract remains [BORATYPE-SPEC](../BORATYPE-SPEC.md).

The normal build uses a 53,264-byte resident image and 11,264 bytes of BSS,
leaving 496 bytes below the package ceiling. Original art and sound/fonts are
embedded parts; cold file/menu/codec work is an eager C overlay. The only
later drawing claim is an 8KB movable band. The accepted XT volume uses
163.5 KiB of app-owned memory; the source-audited startup peak matches that
steady snapshot. Mega grants the region and band high memory and rounds the
region differently, giving 164 KiB. Whole-machine loader high-water is not
measured. No OS budget was raised.

Initial measurements exposed busy unchanged scans and oversized cosmetic
updates. Geometry/signature caches, bounded prompt cells, deferred secondary
fields, one-row portrait work and four-row caption strips repair those paths.
The performance probe uses 30 actual hardware-enqueued keys and 220 periodic
callbacks on a 4.77 MHz MartyPC XT, with sound enabled and the shipping UI
stack guard. Its 100ms assertions remain enabled. Framebuffer completion,
complete callbacks and CRT scanout are distinct costs; see the report for
final figures and the full-paint exception.

Technical guest WAV captures established FM and speaker activity, muting,
route changes and cleanup. They do not establish subjective musical quality.
The audio forwarding tool explicitly refused audio input, so listening remains
unverified. Voice clips and their importer were removed when the owner chose
sound effects and visual messages.
