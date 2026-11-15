# Soak speed, pass 2 — the QEMU and 86Box stream ("qemubox")

The previous pass is docs/plans/completed/SOAK-SPEED.md; its numbered
techniques are cited by number here (14: QEMU waits on guest ticks, 15: the
QEMU mouse closed-loop, 18: 86Box gated on the kernel).

## What was still slow, and why

Ranked by declared `secs` x rows over the QEMU and 86Box rows
(`tests/suite.py`), the scripts that still paid host time per gesture:

| script | rows / declared s | what it paid |
|---|---|---|
| tests/waverack.py (+ edit, play) | 3 / 1,050 | every gesture through `tools/mouse.py` (one Python process, pin on the clamp, walk back on host sleeps: ~1.7 s a click), a host `time.sleep` after each (0.1-1.5 s, 50-odd of them), and a HOST-clock `until` whose budgets a loaded host spent before the guest had |
| tests/xmcheck.py `click`/`dblclick` | the waverack family, httpmount, netinst(bad), httpinstall, viddiskcpu, rdxms | `click` was still a `tools/mouse.py` spawn; `dblclick` a closed `goto` plus host-paced `sleep 0.08/0.12` edges |
| tests/telansi.py `Mouse` (+ telzm, wireball's Qemu) | telansi, telzm, mega-telansi, mega-telzm, wireball, walink* / ~1,250 | `tools/mouse.py` a gesture, `gsleep 0.4` after; `quiet()` polled at 0.25 s |
| tests/ethernet.py `Mouse` | ~12 browser/ethcfg/ftpd/lemmings/rltune rows + box-ethernet | closed-loop already, but every gesture then paced a FIXED 0.15-0.8 guest s |
| tests/brnav.py, brdownload.py, ethcfg.py | menu drags and the link clicks as raw `tools/mouse.py` |
| tests/win16pbrush.py | 1 / 240 | `tools/mouse.py` a gesture and 2-3 HOST seconds after each: ~4.3 s a click, ~20 clicks |

## Changes (goal in brackets)

1. **WaveRack family on the guest's clock** [1, 2] - tests/waverack.py:
   `guest(q)` names the machine every wait counts on; `until` is
   `os88qemu.acted` (guest seconds, poll <= 0.05 - technique 8); `idle(cap)`
   is `ui_done` capped at the host sleep it replaces (technique 14);
   `drained(cap)` ends a typing gap when the BIOS ring is empty (and keeps a
   burst from overfilling the ROM's 15 entries); `gpause` is a pause that IS
   the test (a loop left to play); the open/settle holds (0.5/0.6 s) are on
   `gnow()`, guest seconds. `mouse()` is closed-loop (technique 15) with
   `tools/mouse.py` as the fallback for an unresolvable kernel, and RAISES
   `MouseError` when the pointer stops short instead of letting the row find
   a missing selection twenty steps later. One QMP connection for the run
   (`Q(..., keep=True)`; `shot` releases it for tools/shot.py). waverackedit
   (22 sleeps) and waverackplay (14) take the same verbs; the two sleeps
   that wait for the HOST's wav writer stay host sleeps.
2. **xmcheck.click / dblclick closed-loop** [1, 2] - `os88qemu.mouse_click`
   / `mouse_dblclick` (the four edges in one burst), dead-reckoned only when
   `goto`'s closed loop cannot run. `xmcheck.qm(sock)` is ONE reader object
   per socket: it carries the last press's tick, which `os88qemu._sep`
   needs - a fresh `_Sock` per call would let a click that follows a short
   `ui_done` land inside the previous one's 9-tick double-click window.
3. **telansi.Mouse closed-loop** (telzm and, through telansi.Qemu,
   wireball's Machine, use it) [1, 2]: `ui_done(cap=0.4)` where it was
   `gsleep 0.4`; `quiet()` polls at 0.05. wireball.Mouse likewise, falling
   back to the walk on a MouseError (its serial-mouse contest eats the first
   packets).
4. **ethernet.Mouse's pauses are ceilings** [1]: each fixed `pace` after a
   gesture is `ui_done` capped at it (`_idle`); `_m()` is `xmcheck.qm`;
   `rclick` added (closed-loop). brnav's two menu drags, brdownload's link
   click and right-click, ethcfg's menu drag on it.
5. **win16pbrush's gestures closed-loop** [1, 2]: `os88qemu.mouse_to` /
   `mouse_button` and `d.idle` (ui_done capped at the old host sleep), as
   win16sol and win16calc already wait; the Save As name typed in process;
   the save poll 5 -> 1 host s (a host file read).
6. **`OS88_EMU=qemu`: the os88marty API answered by QEMU** [3] -
   tools/os88qemuapi.py's `ApiQemu`, the shape of tools/os88box86api.py:
   `cmd(**kw)` served over one held QMP connection (memory by `pmemsave`,
   writes through the GDB stub on a private Unix socket, keys as qcodes
   confirmed by the ROM's ring like os88qemu.key, the mouse as `rel`/`btn`
   input events, `fbuf` by `screendump`, the text screen out of B800/B000).
   `status()["cycles"]` is the BIOS tick count in GUEST_HZ cycles, so every
   os88marty guest budget means machine seconds to a tick. os88marty's
   `emulator()` and `_launch` take `qemu` (an additive edit). So
   `os88ui.boot` / `open_drive` / `path` / `menu_pick` / `raise_window` /
   `ui_done` / `key_taken` run on QEMU unchanged. What it has not got is a
   named MartyError each: breakpoints, steps, snapshots (`Forks`), FDC
   counters (`disk()` is {}), CGA/EGA/Hercules/MDA, a <640KB machine.
   docs/TESTING.md's matrix carries it as one row.

## Instance management (goal 2) - what was checked

* QEMU: tests/os88qemu.py has given every instance its own RUNDIR, socket
  and pidfile since SOAK-PARALLEL 17 (`paths`, `workdir`, `Private`,
  `launch_test`, `make_test`), and kills by the pid it wrote. The QEMU
  rows found naming `build/qmp.sock` name it in COMMENTS (the history of
  the fix), not in a launch. ApiQemu follows the same rule, under
  $OS88_QEMU_DIR when the runner hands a row one.
* 86Box: tools/os88box.py gives each instance its own run directory,
  automation socket and GDB port (`free_port`); `launch` refuses a NAME a
  live instance holds. Five callers passed a FIXED name (vbt286, rdgprof,
  pravetzboot, rdghd's default, gbcal), so a second copy of one row - or a
  run killed with its 86Box alive - refused the next run of that row:
  vbt286 and pravetzboot (rows, both `temp=`) now take a pid-suffixed name;
  rdgprof, rdghd and gbcal are tools a person runs and keep theirs.

## Measurements

(filled in by the verification pass after 09:05 UTC - see below)

## Left for the next pass

* lemmings' own dead-reckoned Walker (tests/lemmings.py) and mixer.py's
  1024x768 `tools/mouse.py --screen` walk.
* telansi.Qemu opens a QMP connection per command and reads by `pmemsave`;
  a held `qmp.Client` (as ApiQemu) would cut each read to one round trip -
  but the row hands the socket to children, so it needs `release` points.
* Rows on the closed list (ps2mouse, vmmouse, the RTC ladder, the NE2000
  rows) rewritten against os88ui on `OS88_EMU=qemu`.
