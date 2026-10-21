# vga-face - LESSONS

What the MIDIRack colour face (SPEC.md 105.9.4, 105.9.5) learned the hard way.
Each entry: the trap, what it looked like, and what to do instead.

## Measuring

- **A gesture's count is polluted by the worker.** A playing package's worker
  draws ~20 calls a second, and a one-second window around a key press
  catches them. Attribute every hit by SS:SP (each task has its own stack)
  and report the UI task's alone - otherwise Pause looks like 30 calls when
  it is 7.
- **Count the cell you actually use.** The first colour run of `mrdraw`
  reported Pause as ONE call because the face blits went through
  `OSAPI_GFX_BLITP` (`0x3A4`), which was not in the counted set. Add every
  drawing cell the new face calls.
- **A gesture script must reach its end state from wherever it is.** With
  ten songs, Next plays; with two, it stops. Drive "to paused" in a loop
  that reads the state, not a fixed key sequence.
- **"0 calls" is a finding, not a broken instrument** - if the gesture really
  changed nothing (Stop while stopped). Check the state before believing
  either way.

## The caches

- **A label in the middle of a routine re-scopes its `.locals`.** NASM binds
  `.l` to the last non-local label; a patch-point label (`mrt_vend:`) inside
  a loop made every later `.wrap` / `.d` resolve to the wrong routine. Use
  `..@name` for labels that must not open a scope.
- **Falling through into another routine's pops** - `mru_dyn` "fell into"
  the rack loop and inherited its `.out`, popping registers it never pushed.
  Make each cache walk a proper routine.
- **The library's own redraws must update your cache**: `os88ui_chkhit`
  draws the box it toggled, so tell `mru_ckd`, or the next update draws it
  again (harmless, but it shows up in the count).
- **A card replacing a card must repaint.** Drawing a smaller card over a
  larger one leaves the larger one's edges - "card up draws the card alone"
  is only true from the content.
- **A command that changes AL** - card commands that load a card id - must
  preserve AX: the About handler is called by the kernel.

## The face

- **`mov al, colour` clobbers AX's low byte.** In a pane drawer, setting the
  pen after loading x1 into AX moved the strip's left edge. Push AX around
  every `OSAPI_SET_COLOR` that sits between coordinate loads.
- **Prefix collisions with generated tables.** `mrt_` was `mrtab.inc`'s;
  the wavetable's `mrt_env` collided. Check a prefix against every
  generated include before using it.
- **Header text that overflows** ("Program / Instru") - measure each run in
  cells against the column it sits over; three runs over three columns read
  better than one long string.
- **Check boxes against the right edge** - a 150-px box + label at x2-160
  touches the border; leave 8 px.
- **The kernel draws greyed radio LABELS black on VGA** (the planar
  `font_run` ignores `[gfx_dis]`; the ring does go grey). Known, not yours to
  fix in a face change; mono dithers correctly. Do not chase it as your bug.

## The pictures

- **`os88pkg.py` refuses image compression beside parts.** A 30 KB image
  that packs to 25 would ship 5 KB bigger on every disk - so MIDIRack's
  faces are a sidecar. Decide this before writing the parts plumbing.
- **The system face's advances are EVEN by rule** (SPEC.md 6.4) - set host
  labels by the ink with a constant gap, or "Pause" reads "Pa use".
- **48 = 44 + 4**: a planar face is whole bytes; let the button be the left
  44 and the gap white paper, so five faces at a pitch of 48 are five
  buttons with a gap and BLITP never draws a fraction of a byte.
- **Load at entry, not at paint**: file I/O under the paint's lock is wrong,
  and fetching at first paint shows the fallback for a frame. Decide on the
  PRIMARY at entry (`OSAPI_VIDEO` DH), draw on the window's display.
- **Free the claim on the entry's failure path too** - a claim taken before
  `OSAPI_WM_CREATE` fails is otherwise leaked.

## Looking

- **A scratch script that picks disks by machine name** - "5150" matched the
  CGA machine too and booted 720 KB disks on 360 KB drives: an "autoload
  never happened" that was the script's.
- **CGA dialog arithmetic lies** - a dialog computed at 147 of 155 lines sat
  under the dock. Look; then cut (MIDIRack dropped a row the menu already
  had).
- **86Box rewrites a config on exit** (its own uuid, its key order); commit
  its form once, or every launch leaves the tree dirty.

## What WireFleet's flat face learned (SPEC.md 114.10)

- **A slow paint looks exactly like a broken one.** The identity capture was
  taken two seconds after a dismissal and showed half a repaint - ground, no
  boards, no captions. Nothing was broken: the XT was still composing. Time
  the paint in guest cycles before believing a picture taken after it.
- **The blit is rarely the cost.** BLITP put a 240x24 board row up in 0.7 ms;
  composing that row took 150. Profile with breakpoints INSIDE the row
  (build entry, first cell, each scanline) until the time has a name.
- **`mul` writes DX.** BLITP was refused on every call because the source
  offset was computed with a `mul` after DX had been loaded with the row
  count - zero rows. Its probe had accepted the same rect, which is what
  made it look like the kernel's fault. Count the refusals (`CF`) before
  reading the code a second time.
- **Read every output register of a slot you call between two others.**
  `OSAPI_WM_DISPLAY` answers the screen HEIGHT in BX; WireFleet handed that
  to `OSAPI_FULLSCREEN` as the window pointer, and F hung a VGA desktop on a
  divide fault in `wm_ptr2idx` - shipped, because no row drove it.
- **`os88line_key` edits; drawing is yours.** Typed chat was invisible on
  every face; the link row only ever checked the bytes on the wire.
- **A predicate call between building flags and storing them** (`wf_canfire`
  spends AX) made Fire look live on the opponent's turn.
- **Zero spare clusters decides the format.** apps360.img had none; planar
  tiles packed 1.8 KB worse than packed ones, so the planes are built in RAM
  at the first planar paint instead of shipped. Check every disk the package
  rides BEFORE choosing a storage form.
- **Don't ship what you can stamp.** 34 of WireFleet's 72 tiles were a ship
  segment with the same red X; an AND/OR stamp per format, which the
  converter proves exact for all 34, took the art from 31 KB to 17 KB raw and
  paid for the face.
- **Look at the art zoomed before styling round it.** A stray grey line
  above every ship was the previous ship's keel: the converter cut the atlas
  in equal fifths and the carrier overhung the second one. Segment by the
  pixels, not by arithmetic.
- **A cache key is the caption's STATE.** "Fire at J10" is one buffer whose
  pointer never changes; key the cache on the target, or the caption goes
  stale.
- **Harness traps**: QEMU's sendkey names are `esc`/`right`, MartyPC's are
  `Escape`/`ArrowRight` - a wrong name is silently no key. A plain `make`
  does not rebuild the app's own disk, and a harness reading a stale disk
  against a fresh map reads zeros. A walked-clip BLITP re-enters its cell,
  so `bp_trace` counts each call twice.
