# WireFleet — delivery plan

Two PCs. Two fleets. One winner.

WireFleet is a native os8088 desktop Battleship game for two computers joined
by a null modem. The supplied reference is the visual brief: navy water,
cyan grid, readable gray ships, distinct shot marks, a fleet roster and chat.
The binding contract is SPEC.md §114. No game state belongs in the kernel.

## Delivery stories

Each wave is a reviewable commit pushed to the same draft pull request. The
diary is [HOW-WIREFLEET-WAS-BUILT.md](../HOW-WIREFLEET-WAS-BUILT.md).

- [x] **Foundation:** isolated worktree, contract, delivery plan and generated
  ship source. Establish the draft PR before implementation waves.
- [x] **Deployment:** as a captain I can select, rotate, place, remove and
  auto-place a legal fleet. A native window, About card, controls and color
  and monochrome artwork work on VGA, CGA and Hercules.
- [ ] **Battle:** as two captains we can join over serial, ready our fleets,
  take alternate shots, see misses/hits/sunk ships, and reach one winner.
  The defending computer judges its private fleet; the peer never receives
  unhit ship positions. Chat shares the same framed connection.
- [ ] **Recovery:** silence pauses input without losing a pending shot.
  Duplicate messages are harmless, reconnect reconciles acknowledged turns,
  and saved matches restore ships, shots and whose turn it is.
- [ ] **Presentation:** finite sound cues follow the system's PC Speaker,
  AdLib or Sound Blaster route; CGA has a four-color fullscreen renderer.
- [ ] **Acceptance:** independent protocol faults, two real OS instances,
  three desktop adapters, CGA fullscreen, cleanup, final soak union and full
  gate. Record observations and limitations, with screenshots.

## Existing facilities and the serial boundary

The API index was read first. Use `OSAPI_ABOUT_SET`, `os88ui.inc` controls,
`OSAPI_FONT_RUN`, the clipped desktop blitters, `OSAPI_FSX_*`, file dialogs
and the sound router. Reuse source when sharing behavior; never fork a copy
of WireBall's transport and silently let the two diverge.

WireBall §109 owns the UARTs only inside FSX, masks both mouse interrupt
lines and polls original 8250s. WireFleet needs a mouse and serial together
on the desktop. `mou_isr` reads both UARTs before deciding whose byte it is;
even a disabled interrupt on the spare UART does not protect its bytes.
Therefore a desktop link needs an explicit lease coordinated with the mouse
owner and instance teardown. A naked package IRQ hook is insufficient.
The lease must refuse the active mouse port, avoid changing a winning
mouse's shared IRQ mask, restore UART settings, survive fullscreen entry,
and release on forced close. Its API and budget will be specified before
implementation. No dependency on a 16550 FIFO or scratch register is allowed.

## Verification discipline

Deployment's final rows are `wirefleetdeploy` and `wirefleetart`. They are
registered, with the emulator pass deferred to final acceptance. Wave host
validation is `make` and `make wirefleetdisk`; visual inspection booted
VGA, CGA and Hercules XTs on MartyPC and captured empty/auto-placed fleets.

During waves: assemble with `cpu 8086` and warnings as errors, run host
checks, and boot to inspect the result. Name the affected rows as they are
added. After the final implementation wave: run their deduplicated soak union
once, then `make test-full`. Re-run failures only, per AGENTS.md.

QEMU's two-socket serial bridge is justified by docs/TESTING.md item 3;
MartyPC is the adapter and XT timing instrument. Emulator results cannot
establish a physical null modem's electrical or original-8250 service margin.
