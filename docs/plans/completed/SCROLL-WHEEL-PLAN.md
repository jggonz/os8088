# SCROLL-WHEEL-PLAN — a wheel mouse that scrolls, and what it costs

**BUILT, on `scroll-wheel`, 2026-10-19. SPEC.md §13.18 is the contract and
§13.18.4 has the MEASURED cost; this is the design record behind it.** Below
§9 is the costing as it was written, with ESTIMATED figures. Here is where the
build came out differently:

- **`kern_big` only.** The owner's decision: `kern_small` has no thumb drag
  either and needs no wheel. Its whole cost is the cell's six bytes, because
  the jump table is one ABI on both kernels. That removed §8's question 1.
- **Measured: `.text` +70, `.bss` +26, `.cold` +37, so 133 resident bytes**
  against the ~145 estimated. The wheel's consumer SHARES the key dispatch's
  tail through a DI-indexed record read, which is most of why it is 13 bytes
  and not 35.
- **No `OSAPI_MOUSE_WHEEL` cell.** A driver's wheel rides
  `OSAPI_MOUSE_FEED`'s CL bits 2 to 7. Every driver built before masked them,
  so old binaries report no wheel, and the one decode in `mou_apply`'s tail
  serves the CH375 and both VMware arms (SPEC.md §9.12.8).
- **Three LINES a notch, not three arrow steps.** That was the owner's
  "three". Note Pad's arrow is four rows, so three arrow steps would have been
  twelve.
- **The PicoMEM carries no wheel.** Its BIOS's `int 33h AX=0060h` report has
  no field for one, and v2 stays on stock firmware.
- **The PS/2 knock did not fit `.ovl`,** which had two bytes left.
  `wm_init` moved to the blob on `kern_big` to pay for it, and the first knock
  read its table through the wrong segment (SPEC.md §9.9.8). The row
  `wheelps2` caught it.
- **Parity.** The new cell shifted every hot routine by an odd amount
  (`t_hotpar`). It is six bytes instead of five, and two of my own bytes moved,
  for zero net.
- **Every program with the shared bar got a handler, plus three without it**
  (SPEC.md §13.18.5).

The ask: if a wheel mouse is plugged in, support its wheel. The first use is to
drive the scroll bar of any program that has one, starting with the shared one
(SPEC.md §13.10).

## 0. The answer in one table

| piece | where the bytes live | `kern_big` | `kern_small` |
|---|---|---|---|
| serial IntelliMouse (`MZ`) wheel nibble, decoded instead of dropped | `.text`, resident (ISR) | ~28 | ~28 |
| the accumulator, `[mou_whl]` | `.bss` | 2 | 2 |
| PS/2 IntelliMouse: four-byte phase and the Z byte | `.text`, resident (ISR) | ~22 | — (no PS/2 on small) |
| PS/2 IntelliMouse: the `200/100/80` knock and the ID read | `.ovlw`, freed at the desktop | ~35 | — |
| `ui_task`: take the accumulator and dispatch to the front window | `.text`, resident | ~35 | ~35 |
| `W_ONWH`, a word in the kernel-private record tail | `.bss`, `MAX_WIN` words | 24 | 12 |
| `OSAPI_WM_ONWHEEL`, an inline cell | jump table | ~8 | ~8 |
| the Disk window scrolls (`fm_scroll_by`, already built) | `.cold`, resident | ~12 | ~12 |
| `OSAPI_MOUSE_WHEEL` for pointer drivers (VMware, CH375) | `.text`, resident | ~16 | — |
| `HIBER.DRV` repeats the knock on wake | on-demand module | ~30 | — |
| **resident total** | | **~145** | **~100** |

Nothing in the table crosses a footprint rung today. `kern_big` has 185 bytes
left in its image rung and 48 in its cold rung. `kern_small` has 192 and 46.
Per the banner in CLAUDE.md that is not the point. The question to decide is:
**do we spend ~145 resident bytes on `kern_big` (and ~100 on `kern_small`) so
the wheel scrolls?**

Per program the cost is **~20-35 bytes of package image**, not resident,
once the program opts in (§5). Packages that do not opt in are unchanged and
need no rebuild. There is **no `PKG_FMT` or `DRV_VER` bump**: the window word
is in the kernel-private tail, and both new cells are appended at the end of
the table (SPEC.md §20.2.0.1). SPEC.md §13.11's `W_ONRC` set this precedent
to the byte.

## 1. What exists today

The kernel already **receives** the wheel on one of its four pointer backends
and throws it away on purpose. It has no event to carry it in.

| backend | the wheel today | file |
|---|---|---|
| serial Microsoft / `MZ` (both kernels) | the fourth byte is consumed by position and **dropped** (SPEC.md §9.5.4). That fix is what lets a wheel mouse win the port contest at all | `kernel/mouse.inc` `mou_byte` `.chk2` |
| PS/2 aux port (`kern_big`) | `0xF4` leaves the device in its **3-byte boot mode**. The ID is banked in `[mou_p2id]` and not tested (SPEC.md §9.9.5: *"No wheel and no third button"*) | `kernel/mouse.inc` `mou_p2_init`, `mou_p2_byte` |
| VMware backdoor (`kern_emu`) | EDX is the wheel and is **dropped** (SPEC.md §9.11.4) | `drivers/vmmouse/vmmouse.asm` `vmm_rbody` |
| CH375 USB (driver) | boot protocol, 3 bytes read; *"No wheel"* (SPEC.md §9.12.4) | `drivers/usbmouse/usbmouse.asm` `um_report` |

There is no **consumer** either. SPEC.md §10's event set is press, release,
right press and wake. The window record has no field for a wheel. The shared
scroll bar is **geometry, not policy** (SPEC.md §13.10.1): it draws and
hit-tests, every package owns its own view position and its own arrow-click
code, and nothing outside a package can find its bar block.

## 2. The design: a level, not an event

### 2.1 Producers add to one signed word

Each backend's ISR (or driver) **adds** its delta to a signed word,
`[mou_whl]`, and wakes the UI task. It does not push an event record.

That is the most important decision in the plan. There are three reasons:

1. **The ring cannot be flooded.** SPEC.md §10.1 drops the OLDEST record when
   the 16-slot ring is full. A fast spin is 20-40 notches a second at 1200
   baud. If every notch were a record, a flick during a slow repaint would
   evict a click queued before it. A word cannot overflow in any real use.
2. **Coalescing comes free, and the target machine needs it most.** On a
   4.77 MHz 8088 a three-line text scroll is a blit plus three 78-cell rows,
   about 215 ms (PERFORMANCE.md's 71 ms a row). Delivered per notch, a
   20-notch flick is **~4.3 s of scrolling after the hand has stopped**. As a
   level, the UI task reads the sum once per pass and the package applies it
   in **one** call. A delta at least one page tall becomes one repaint. The
   lag is capped at one repaint behind, however fast the wheel turns.
3. **It is the cheapest form.** The ISR does `cbw` / `add [mou_whl], ax` and
   reuses the wake `mou_apply` already calls. The consumer does
   `xor ax, ax` / `xchg ax, [mou_whl]`. That exchange is one instruction, so
   an IRQ cannot split it and no `pushf`/`cli` is needed. No `EVT_` type is
   added, so the `dec ax` dispatch chain and its `%error` guard
   (`kernel/ui.inc`) are untouched.

What a level gives up is ORDER relative to clicks. A notch turned just before
a click may be applied just after it. A wheel delta carries no position, and
no program can tell the difference.

The sign convention is the one all three wire formats share. **Positive means
the wheel turned toward the user, which scrolls the view DOWN** (the view's top
row increases). The serial `MZ` nibble, the PS/2 Z byte and VMware's EDX all
agree. Linux negates each into `REL_WHEEL` the same way.

### 2.2 The consumer gives it to the FRONT window

`ui_task` checks the word once per pass, beside the event drain. A non-zero
sum goes to the window `wm_top` names, with `fdlg_top_x`'s override while a
modal dialog is up. That is exactly where a key goes. The window gets it
through its `W_ONWH` handler, in `W_ONCLICK`'s environment: gfx lock held,
billed through `ui_bill`, **AX = signed notches**, CX/DX = the pointer, and
SI = the window. A window with no handler costs one word read and the delta is
discarded.

**Why the front window and not the one under the pointer**, which is what a
modern desktop does: SPEC.md §13.11 already binds the right button to
*"frontmost only"*, and the reason applies here with more force. A scroll path
BLITS. `fm_scrollpaint` and every package's `gfx_scroll` assume the window is
uncovered. A background window scrolled by blit would smear over the window in
front of it, unless every package's scroll path were taught the clip region
(SPEC.md §11.3). The window under the pointer is already tracked
(`[cur_hovw]`, `cur_shape_pass`), so a later version could **raise and
scroll** a background window. That would cost one `ui_raise` call, but it is
a behaviour change the owner should decide on and it is not in the first
version.

### 2.3 The callback is a record word, like the right button's

`W_ONWH` goes in the kernel-private tail of the window record (`kernel/wm.inc`,
beside `W_ONRC`). It is installed by a new inline cell, `OSAPI_WM_ONWHEEL`:
`mov [cs:bx+W_ONWH], ax / retf`. That is `OSAPI_WM_ONRCLICK`'s shape exactly.
`wm_destroy` and `wm_init` already zero the tail in one run, so a reused slot
cannot inherit a stranger's handler. The record grows by one word: **24 bytes
of `.bss` on `kern_big` (12 windows) and 12 on `kern_small` (6)**, the bill
SPEC.md §13.11 paid. WIN_SIZE goes 72 → 74 and 65 → 67, still well inside the
127 that keeps `[bx+disp8]`. `tools/os88geom.py` mirrors WIN_SIZE, so it moves
in the same commit (`t_mirror` checks it).

## 3. Per backend

### 3.1 Serial `MZ` — both kernels, ~28 bytes

At phase 3, a byte with bit 6 clear is already recognised as the wheel byte
(SPEC.md §9.5.4). It becomes:

- **settled port only.** Test `[mou_seen]` the way `.pkt` does. An unsettled
  device's bytes must not scroll for the same reason its packets must not
  click: a modem's lowercase text has bit 6 clear too.
- **the low nibble, sign-extended from bit 3** (-8..+7). Bit 4 is the middle
  button, which SPEC.md §10 has no record for, so it is dropped as today.
- **zero is free.** A Logitech three-button (`M3`) sends its fourth byte only
  as `0x20` / `0x00` for the middle button. Its low nibble is always 0, so it
  can never produce a wheel delta. One `jz` keeps that path one compare
  longer than today.

What the kernel cannot do is **ask** for the wheel. A serial IntelliMouse
announces `MZ@` in its PnP identify string (SPEC.md §9.4.1 reads that burst)
and then sends four-byte packets without being told. So the serial half needs
no probe at all, and a three-byte mouse costs nothing new: its next header
lands at phase 3 exactly as it does now.

### 3.2 PS/2 IntelliMouse — `kern_big`, ~22 resident + ~35 overlay

A PS/2 wheel mouse powers up as a three-byte mouse and enters wheel mode only
when it is **knocked**: sample rate 200, then 100, then 80 (`0xF3` + rate,
each acknowledged), then `0xF2` to read the device ID. An ID of `0x03` means
packets are four bytes from then on, with byte 3 the signed Z delta.

- **The knock goes in `mou_p2_init`**, between the device reset and `0xF4`.
  It reuses `mou_p2dev`, which sends a command and collects its ACK, and
  `mou_p2rd`. That is about seven calls. `mou_p2_init` is `.ovlw`, freed by
  `mem_unblob`, so **these bytes are not resident**. The ID replaces what
  `[mou_p2id]` banks today, so `MOUDIAG=1`'s report says what mode the mouse
  ended in.
- **The decoder counts to four** only when the ID was 3. `.b2` today resets
  `[mou_p2ph]` to 0 and acts on the packet. In wheel mode it sets the phase to
  3 and still acts on the motion at byte 2, so motion lag does not grow by a
  byte. Phase 3 takes the Z byte. This must be a real phase and not a
  tolerance: byte 3 can have bit 3 set, and bit 3 is the only sync PS/2 has,
  so a decoder expecting three bytes from a four-byte device misframes on
  every packet.
- **Leave the rate at 80** reports a second, the knock's last value. That is
  20% fewer IRQ12s than today's 100, which a 286 does not mind. Putting it
  back to 100 costs ~6 overlay bytes if anyone wants it.
- **Every path that re-arms the mouse must knock again**, and this is the trap
  in the PS/2 half. SPEC.md §96.45.2.2's wake re-arm sends only `0xF4`. After a
  power-off hibernate, the device comes back from POST in three-byte mode. After
  a DOS run, a DOS mouse driver may have reset it. Either way the kernel would
  still expect four bytes, and **the pointer would be garbage, not merely
  wheel-less**. So `hbm_wake` repeats the knock. It is in `.modh`, which is
  `HIBER.DRV`'s image, so that is ~30 bytes of an on-demand module and none
  resident. If the ID that comes back is not 3, the decoder falls back to
  three bytes. **This half is QEMU-only to verify** (docs/TESTING.md's list,
  entry 4), exactly as §96.45.2.2's re-arm is.

`kern_dos` decodes the serial mouse only (`kerndos/kdmouse.inc`), so it is
unaffected. A DOS mouse driver started after `ui_cmd_reboot` resets the device
itself.

### 3.3 VMware backdoor — `kern_emu`, ~16 kernel + ~8 driver

`vmm_rbody` already has the wheel in DL. It passes it through a new appended
cell, **`OSAPI_MOUSE_WHEEL`** (AX = signed notches). The cell is fenced to the
pointer driver class like `OSAPI_MOUSE_FEED` (SPEC.md §9.12.1) and ends in the
same `add [mou_whl]` / wake. QEMU's `pc` machine sends the wheel through its
vmmouse, so `make vmmousetest` can drive it.

### 3.4 CH375 USB — deferred

Many boot-protocol mice append the wheel as a fourth report byte. The HID boot
protocol permits that but does not require it. `um_report` would read
`[um_buf+3]` when the report is long enough and call the same cell, about 12
bytes of driver. **No emulator carries a CH375 and no Book8088 has run the
driver** (SPEC.md §9.12.4), so the only thing it could be tested against is
`CH375SIM`'s model, which would have to be taught a fourth byte. It waits until
the driver has had its first field run.

## 4. The kernel's own scroll bars

**The Disk window works with no package involved.** Its scroll paths all go
through **`fm_scroll_by`** (`kernel/files.inc`): SI = window, AX = signed rows,
CF = 1 means the caller owes `fm_repaint`. That includes keys, arrow clicks,
paging and the thumb drag. Its window creation stores a kernel handler in
`W_ONWH` that multiplies notches by a step and calls it, ~12 bytes. The file
dialog *is* a Disk window in a chooser role (`kernel/fdlg.inc`), so it scrolls
too.

Two scroll behaviours worth noting:

- **Arrow keys move the selection; the wheel moves the view.** On `kern_big`
  with a live selection, the arrow keys move the selection rather than the view
  (`fm_onkey_x`). The wheel moves the view and leaves the selection alone,
  which is what users expect.
- **One notch is one row**, because Disk window rows are tall, against three
  lines in a text view.

The Control Panel's lists are arrow-only, with no thumb or page
(`cp_lscroll`). They are left alone in the first version: the panel is
`CTRL.DRV`, and a wheel there is a module change with its own price.

## 5. Programs: opting in

The shared bar has no scrolling behaviour to call, so **each program opts in**
with one install and one handler. What the handler costs depends on what the
program already has:

| shape | programs | handler |
|---|---|---|
| a **by-N** scroll already exists | Browser (`br_scroll_by`, which already coalesces bursts), The Wire (`wr_scroll`), Frotz (`zw_scrollpage` / `zw_scrollto`) | multiply, call. **~15-20 bytes** with the install |
| **absolute** scroll-to only | Note Pad (`np_scrollto`), Word (`wd_scrollto`), Scribe (`sc_scrollto`), Sheet (`sh_setscrollrow`), Redline (`ru_spto`), Loom (`lm_view_to`), Weave (`w_lscroll`) | top + notches × step, clamp, call the arrow path's own `.set` tail, which already redraws and moves the thumb. **~25-35 bytes** |
| inline in the click handler | TeXpad (two bars), Task Manager, MIDI Rack (`mr_scroll` takes a PART code) | factor the arrow arm into a routine first. **~30-40** |

One library helper keeps the second row honest and small:
**`os88ui_sbwheel`**, opted into with `%define OS88UI_WHEEL`. It takes BX = the
bar block (the package has just filled it with `xx_sbset`, as before every
`os88ui_sb*` call), AX = notches and CX = the step per notch. It returns AX =
the new position, clamped to `0..total-fit`, with CF = 1 when that equals
`pos`, so nothing is redrawn for a wheel turned against the end. That is the
same job the thumb drag's `os88ui_sbtrack` already does: turn a gesture into a
`pos` and let the package scroll. ~25 bytes, in the carriers that ask for it.

**Notches to lines**: `OS88UI_WHL_STEPS equ 3` arrow steps a notch, the
Windows default. On the 8088 three lines cost little more than one, since the
blit dominates.

The C SDK needs `os88_wm_onwheel()` in `apps/cc/os88.h` and its thunk before
Loom and Weave can opt in. The C rules (docs/C-TOOLCHAIN.md) are no obstacle:
the handler takes an int and calls a static function.

**Suggested order:** the Disk window (free with the kernel work), then Note Pad,
Browser and Word. Those three are the programs whose documents are long enough
for a wheel to matter, and they cover all three handler shapes.

## 6. Refused, and why

- **Turning a notch into Up/Down arrow keystrokes.** It costs no record field
  and no package changes. It is what period mouse drivers did. It is refused as
  the default because in five of the programs above (Sheet, The Wire, Note Pad,
  Word's caret, Loom) the arrows move a **cursor or selection**, not the view.
  That is not "controls the scroll bar". A program that wants exactly the
  arrow behaviour can call its own key handler from `W_ONWH` in three bytes.
- **Clicking the bar's arrow for the program.** This is the only design that
  would reach every library-bar program **with no source change**:
  `os88ui_sbar` would record the bar's rectangle through a cell each time it
  draws, and the kernel would synthesise a press and release on the arrow.
  It is refused on four counts:
  - A window moved by blit keeps a **stale absolute rectangle**, and a
    synthetic click on the old spot lands in the document: it moves the caret
    or drops the selection.
  - Two-bar programs (TeXpad, Sheet, Redline's panes) scroll whichever bar was
    drawn last.
  - Synthetic presses would have to be kept out of the kernel's double-click
    stamp.
  - It makes every package's click handler part of the wheel's contract
    without any package having agreed to it.
- **A wheel `EVT_` record per notch.** See §2.1: it floods SPEC.md §10.1's ring
  and puts one repaint per notch on the slowest machine.
- **Sending it to the window under the pointer.** See §2.2: deferred, not
  refused.
- **An on-demand module.** The decode is in two ISRs and the consumer is ~35
  bytes on the UI task's own path. Nothing here is big enough or cold enough to
  load from a disk.

## 7. Verification

Each row follows docs/WRITING-TESTS.md, including §1's rule: **break it on
purpose and watch it go red.**

| row | instrument | asserts | red when |
|---|---|---|---|
| `wheelps2` | QEMU, `MOUSEPORT=ps2` (docs/TESTING.md entry 4). QMP `input-send-event` with `wheel-down` / `wheel-up` is a real IntelliMouse Z byte in QEMU's i8042, which implements the knock | `[mou_p2id]` = 3; N notches move the Disk window's and Note Pad's top by N × step; notches sent during a repaint arrive as **one** handler call (a counter) | knock removed (ID 0, no scroll); the four-byte phase removed (the pointer misframes, so movement is asserted too) |
| `wheelser` | MartyPC, 5150, serial | the same through the `MZ` nibble; and **an existing three-byte-mouse row is still green**, since that is the case the phase change could break | nibble decode removed |
| `wheelvmm` | QEMU, `make vmmousetest` on `kern_emu` | the same through EDX | the driver's `add` removed |
| `mouresume` (existing) + a PS/2 leg | QEMU | the pointer still moves after a hibernate wake, and the wheel still scrolls | `hbm_wake`'s knock removed |

**MartyPC needs a patch first.** Its Microsoft serial mouse emits three-byte
packets only (`tools/martypc/debug_server.rs`'s `mouse` builds them). A
`15-serial-mouse-wheel.patch` is a fourth `MZ` byte, emitted only when the
machine config asks for a wheel mouse, so every existing row's byte stream is
unchanged. The debug server's `mouse` request gains a `wz` field. Estimate:
40-60 lines of Rust. QEMU's own serial mouse (`chardev/msmouse.c`, 8.2) is
Logitech-style and sends no wheel at all, so it cannot stand in.
`tools/mouse.py` and `tools/os88mouse.py` each gain a `wheel N` verb.

Field: the 100 MHz Pentium behind docs/FIELD-NOTES.md 44 has a wheel mouse on
a serial adapter and is the natural first field run of §3.1. The owner's 5150
mouse is a three-button Microsoft without a wheel.

## 8. Decisions for the owner

1. **Does `kern_small` get the serial half?** ~100 resident bytes on the build
   under a standing cut (SPEC.md §39.27.4 banks reclaims rather than budgets
   them). 128KB XTs with serial wheel mice exist but are rare. The
   recommendation is `kern_big` first, with the small arm kept as a separable
   `%ifdef` priced on its own.
2. **Front window only, or raise-and-scroll the window under the pointer?**
   (§2.2). The recommendation is front only for now.
3. **Notch size**: three arrow steps (Windows) or one (classic Mac).

## 9. Waves, if it is taken

1. **Kernel**:
   - `[mou_whl]` and the `MZ` nibble.
   - `W_ONWH`, `OSAPI_WM_ONWHEEL` and the `ui_task` consumer.
   - The Disk window's handler.
   - SPEC.md §9.5.4, §10, §11 and a new §13.x for the callback, written
     *before* the code.
   - `t_mirror` and `abilock --update`.
   - The MartyPC patch and `wheelser`.
2. **PS/2**: the knock, the four-byte phase, the `hbm_wake` knock, `wheelps2`
   and the `mouresume` PS/2 leg.
3. **The library and the first three programs**: `os88ui_sbwheel`, then Note
   Pad, Browser and Word.
4. **The rest of §5's table, the C SDK, and VMware**, each a few bytes in its
   own package.
