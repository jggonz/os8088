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
- [x] **Battle:** as two captains we can join over serial, ready our fleets,
  take alternate shots, see misses/hits/sunk ships, and reach one winner.
  The defending computer judges its private fleet; the peer never receives
  unhit ship positions. Chat shares the same framed connection.
- [x] **Recovery:** silence pauses input without losing a pending shot.
  Duplicate messages are harmless, reconnect reconciles acknowledged turns,
  and saved matches restore ships, shots and whose turn it is.
- [x] **Presentation:** finite sound cues follow the system's PC Speaker,
  AdLib or Sound Blaster route; CGA has a four-color fullscreen renderer.
- [x] **Acceptance:** independent protocol faults, two real OS instances,
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
and release on forced close. SPEC.md §115 pins its API, ownership and budget;
`SERIAL.DRV` implements the lease for both kernels. No dependency on a 16550
FIFO or scratch register is allowed.

## Verification discipline

Wave host validation used `make` and `make wirefleetdisk`; visual inspection
booted VGA, CGA and Hercules XTs on MartyPC. Final acceptance passed all nine
affected rows: `wirefleetart`, `wirefleetdeploy`, `wirefleetlink`,
`wirefleetsystem`, `wireball`, `wireballmono`, `kernmods`, `mouwheel` and
`hibernate`. Failed rows alone were rerun after repairs. The system row also
checks competing instances, a real 8250 with both serial IRQs masked,
CRC-valid invalid saves and actual speaker/FM note grants.

The clean build passed 52 host checks. All 60 full-suite rows passed; the
earlier PS/2 run needed a separate retry after overlapping QEMU sessions held
the same image's write lock; the merged-base full run passed all 60 together.
The fixed big/small/emulator memory budgets
pass, with 3,072 / 2,560 / 3,072 bytes spare. The diary contains screenshots
and the remaining physical-hardware limits.

During waves: assemble with `cpu 8086` and warnings as errors, run host
checks, and boot to inspect the result. Name the affected rows as they are
added. After the final implementation wave: run their deduplicated soak union
once, then `make test-full`. Re-run failures only, per AGENTS.md.

QEMU's two-socket serial bridge is justified by docs/TESTING.md item 3;
MartyPC is the adapter and XT timing instrument. Emulator results cannot
establish a physical null modem's electrical or original-8250 service margin.
