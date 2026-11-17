# Olivetti PC1 mouse — delivery plan

The source supplied for this work is `pc1mouse.asm` at the repository root.
It is a DOS INT 33h driver for the Olivetti Prodest PC1's Yamaha V6335D
controller, not a serial or PS/2 mouse driver.

## Goal

Make the PC1's built-in mouse drive os8088's native pointer, clicks and normal
desktop events. Keep the OS cursor renderer and use the OS's `DRVC_POINT` /
`OSAPI_MOUSE_FEED` path. Do not install the DOS driver's cursor or take over
IRQ0/IRQ1 as its resident program does.

## Stories

- [ ] **PC1 detection and attach:** the optional driver identifies a PC1 from
  the documented BIOS signature before touching the V6335/8042;
  absent or incompatible hardware refuses without changing machine state.
- [ ] **Movement:** the driver's worker samples and clears the V6335 X/Y
  counters, translates their signs/scales to OS-relative deltas, and feeds
  reports through `OSAPI_MOUSE_FEED`.
- [ ] **Buttons:** both PC1 mouse buttons are delivered as normal left/right
  OS mouse state without losing or reinterpreting ordinary keyboard input.
- [ ] **Selection and lifecycle:** PC1 and USB are alternative publishers of
  `DRVC_POINT`; the user can enable the PC1 row, and detach stops the worker
  and releases any held buttons.
- [ ] **Field validation:** build a dedicated PC1 test disk and document a
  real-machine check for attach, cursor movement, both buttons, keyboard
  operation, detach/restart and the standard OS cursor on the PC1 display.
  Include what a click does after the row is unticked and inside a DOS
  session: the controller keeps sending 77h/79h until restart and the kernel
  no longer consumes them there (SPEC.md 9.15).

## Verification

Host checks must build the driver and every system-disk geometry and keep the
fast regression tier green. No emulator in this tree models the V6335D mouse;
the hardware protocol, sign, scale and 8042 interaction require the owner's
Olivetti PC1. Record those readings as field evidence rather than claiming an
emulator test proves them.

The driver and 720KB field image are built. The remaining open item is to
record the movement, button, keyboard and restart results from the real PC1.

The first PC1 run detected the mouse, but vertical physical motion moved the OS
pointer horizontally and the horizontal response was too fast. The driver now
maps counter 11h to X and negated counter 10h to Y, with neither doubling nor
acceleration. The corrected image is ready for a second field run; this mapping
and sensitivity still need confirmation.

The DOS reference's button setup first queries with command 12h, then sends
`12h,77h,78h,79h,01h` as five data-port bytes. The initial port sent the
second 12h to the command port instead, so clicks might not have been enabled.
The setup now matches the reference sequence exactly; the image is rebuilt for
the next field run.

The owner then confirmed that the direct-connected original quadrature mouse
works under DOS, that clicks are recognized on the PC1 image, and that the
cursor drifts right while the mouse is stationary. p1_sample was loading each
button scancode into AL while AX still held the horizontal delta; the 79h
right-button query therefore became a positive X movement on every poll. AX is
now saved across both key queries, with `tests/unit/t_pc1mouse.py` guarding the
ordering.

The PC1 make/break codes were also reaching the BIOS keyboard buffer, where
they were interpreted as ordinary navigation keys. With the PC1 row attached,
the kernel's existing INT 09h path now updates the key-state map and consumes
77h/78h/79h before chaining to the BIOS. The worker still reads the left/right
levels through `OSAPI_KEY_DOWN`; `t_pc1mouse` guards both the X-delta bank and
the keyboard filter.

Field experiments with alternate axis maps and a slower scale are no longer
part of the gate: the owner selected the DOS-reference mapping, so
`make pc1mousetest` now builds only `build/pc1mouse720-ref.img`.

On the PC1, the diagnostic image showed the left-button scan `77h` reaching
the kernel filter (`x=33`) without the BIOS enqueueing `7708h`; the owner
confirmed the result looks correct. The implementation now places the filter
inside the shared `KBM_SCAN` body, before its early chain exits. This matters
on kern_big: its XT INT 09h path uses the `mem` expansion because the same
kernel also contains the AT INT 15h path. The XT-only handler pulses bit 7 of
port 61h to acknowledge the 8255, then sends IRQ1's EOI. `tests/unit/
t_pc1mouse.py` guards that ordering and the field-diagnostic marker.

The DOS reference mapping and the click filter have been checked on the real
PC1. The remaining lifecycle checks (detach/restart and ordinary keyboard
operation across those transitions) should be reported separately if they are
not part of the owner's final field pass.

## Design constraints

`DRVC_POINT` has one publication slot. The existing USB mouse and PC1 must be
separate optional rows of that class, so only one may be attached at once.
The driver never leaves an interrupt vector or display register changed. Its
brief IRQ1 mask is restored on every return. The PC1 button mapping follows
the supplied DOS driver's session-long setup and remains configured until
restart; detaching stops polling and releases any held OS button. The OS's
mouse events and cursor remain authoritative.
