# Original-grid portrait source and runtime

`provided-animation-grid.png` and `provided-lesson-scene.png` are the exact
user-supplied images. `ANIMATION-SOURCE.json` records their hashes, grid/crop
coordinates, palette and selection rules. The older generated master/prompt
are historical; the active portrait source is the supplied grid.

`tools/os88btanim.py draft GRID SCENE` is a one-time Pillow authoring operation.
It crops all thirty source cells without their grid labels/borders. Idle/blink
uses the neutral body with the source eye patch, and talk uses the source mouth
patch, so small motion does not make the camera/jacket jump. Praise, roast and
facepalm preserve the whole original gesture. The scene is the unobstructed
sky/mountain/yurt strip from the supplied lesson, behind the original navy mask.
The committed animation.txt is authoritative indexed source; sprites.txt and
blink.txt select matching static/mono/CGA poses. The explicit mono threshold
and EGA mapping are deterministic. No generative service is used.

`build` reads only text and existing standard-library packers. Every frame has
five large24-row strips, two compact-color strips and two explicit-mono strips.
The planar output is already in shared bx_out's format. Existing os88lz LZ4
packs each strip, and every stream is independently decoded and compared with
its exact expected pixels. The30frames×9strip directory lives in bank3:

- Header16bytes: `BTAN`, versionLE16=1 at4, exact bank lengthLE16 at6.
- Directory starts at16; each8-byte entry is part,offset,packed_length,signature,
  allLE16. Index=`frame*9 + variant_offset + strip`, offsets0/5/7 for
  large/compact/mono. Signatures identify identical pixel strips.
- Banks4/5 have the same16-byte header. All three remain below65,000bytes.
- Bank5 also carries the lead's scene offsets/cards after its header. The
  normal sprite helper can temporarily select that bank; it is restored by
  the caller. Card sizes/selection belong to os88btscenes.py.

Parts0/1/2 remain art/sound/eager-code. Optional lazy parts3/4/5 are fetched once
in face initialization, then marked movable. Read-only instances use static
source artwork; fetch failure drops partial banks and retains that fallback.
No clock/paint/key call reads files. Strip lookup re-reads each live part segment,
validates its header and extent, then the small native `bt_anim_unpack` bridge
calls OSAPI_DECOMP into the existing8KBdrawing band. The system decoder's exact
output bound is120/480/1152bytes. bx_rows/bx_new/bx_out retain the normal armed
window clip and planar/packed fallback; no VRAM or new decoder is introduced.

`bt_face_clock(now)` is called after scoring/reaction/audio service and before
the platform's pending-input redraw fence. The resident wrapper also calls the
PLAY clock hook when present. Cosmetic selection is in the eager overlay.
Idle blink closes/opens for two ticks per frame late in a91-tick cycle, talk
runs for36ticks with three-tick frames, and one-shot gestures have six five-tick
frames before returning to idle. Rendering coalesces to the latest target.
One idle call emits at most one24-row strip. Blink changes at most two strips;
unchanged body/background strips are skipped. Caption retains its separate
four-row work bound. Urgent prompt/key work stays ahead of all of these jobs.

The band-only `ovl_bt_portrait_scene(x,y,w,h)` centers the96×120 or40×48 source
pose and records its actual picture bounds; callers own frame/caption. It is
safe within full menu/body band loops. The same idle service handles a visible
menu mascot without drawing a native image underneath a later band.

Verification on this lane: deterministic stream round trips and all-bank
header/extent checks; 24 color/mono production-face layouts under ASan/UBSan;
actual decoding activity across every family; blink dirty-strip limit; tick-wrap
handling; zero output for unchanged state; byte-identical session state;
reversed-Shift and frozen-clock production mutations rejected. Native compiler
and package gates pass. The timed raw limit is1152bytes; source target cadence
is about3.6gesture frames/s when idle work gets one job each OS tick. Actual
MartyPC cadence and queued-key latency remain the lead's integrated target
measurements, not a claim made by host checks.
