# Soak speed, pass 2: the MartyPC harness core

The stream that owns `tools/os88marty.py`, `tools/os88ui.py`,
`tools/os88mouse.py`, `tools/os88mouserel.py` and the MartyPC debug server's
client side. Every MartyPC row (~1000 soak rows, 26.5 h of declared `secs`)
pays what is here, so a fixed cost cut here is cut ~1000 times.
docs/plans/completed/SOAK-SPEED.md is the first pass and its numbered
techniques are cited as "SOAK-SPEED n".

## Changes

1. **A warm boot cache across rows** - `tools/os88bootcache.py`, used by
   `os88ui.boot` when `OS88_BOOTCACHE=1`. OPT-IN.
   - A miss boots the machine once, DETACHED (label `bootcache`), to
     `kernel_up`'s gate, turns the saver off unless `saver=True`, pauses and
     takes a fork snapshot (debug_server.rs `snapshot`; SNAPSHOT-PLAN B). The
     registry process stays up, paused, holding it.
   - A hit connects to the registry under an flock, `restore`s onto a free
     port and lets go: ~0.1 s for what was a 15-30 guest-second boot plus the
     emulator start (and a reflink copy of each floppy).
   - The KEY: machine, sha1 of both floppies (after `$OS88_BUILD`), the
     emulator binary and its `configs/` and `media/roms/` (path, size,
     mtime), the gate card, the saver choice. Not cached (`eligible` says why
     and the row boots fresh): any `launch` keyword but `label`/`timeout`
     (`extra`, `rom`, `run_dir`, `addr`, `detach`, a `boot=` number),
     `$OS88_ROM`, `$MARTYPC_WAV`, `$OS88_MARTY_BIN`, `$OS88_MEGA`, a non-Marty
     emulator, and a machine whose name says it has a disk or device that is
     written THROUGH rather than held in RAM (`hdd`, `ide`, PicoMEM, NIC) - a
     fork does not roll a VHD back.
   - THE DISKS. A floppy is RAM in MartyPC (nothing writes one back but
     `flush`), so each restore starts from the snapshot's sectors and writes
     privately. The server's default flush path is the REGISTRY's run tree,
     so a restored Marty (`CachedMarty`) gets its own tree with copies of the
     floppies and `flush()`/`mount()` resolve there. It is registered as an
     instance owned by the row, so `instances`/`reap` see it, and close()
     kills it.
   - Lifetime: entries idle `OS88_BOOTCACHE_IDLE_MIN` (20) minutes, or past
     `OS88_BOOTCACHE_MAX` (6), are killed by the next row that uses the
     cache. `python3 tools/os88bootcache.py list|drop|check IMG [APPS]`.
     Zombies do not accumulate: a live restore is the PARENT of the next
     holder, so only the first restore of a registry is its child.
   - What a row can see: the guest clock continues from the end of the boot
     (as if that boot were its own), and the boot's work is not repeated - a
     boot-subject row uses `launch`, which this never touches.
   - `check` is the self-check: a cached and a fresh boot of the same key,
     compared on the desktop's state words and the rendered frame.

2. **`launch`'s start-up polls** - the port file every 20 ms (was 0.25 s)
   and the first answer every 50 ms (was 0.5 s), on the same deadlines (60
   s, 40 s). Paid on every launch of every row.

3. **`until`'s adaptive poll tops out at 50 ms** (was 250, `OS88_UNTIL_NAP`):
   a long wait ended on average 125 ms after its condition was true -
   SOAK-SPEED 8's own rule. And its `status` feeds `_Progress.check(c)`, one
   round trip a poll fewer.

4. **`ui_idle` is one pipelined round trip** (`_reads1`, `Marty.batch`):
   five serial reads a look, and `ui_done`/`key_taken`/`settle`'s short
   window look many times a wait. Falls back to the serial reads on a
   non-Marty object or a server that does not pipeline.

5. **TCP_NODELAY on the client socket too** (SOAK-SPEED 17; the server
   already set it). A request is one write, so this is insurance against a
   thread writing between a request and its reply.

6. **`Marty.restore` polls 10 ms** for the restored machine (was 100):
   every `Forks.restore()`.

7. **OS88_WAITLOG's `between` lines and `os88marty.py waitlog FILE`.** Each
   logged wait now writes, first, the host and guest seconds since the last
   logged wait on that Marty ended - the row's own work and the harness's
   overhead between waits, which the log did not have. `waitlog` sums waits
   and gaps per call site, heaviest host first: the work list for the next
   pass. A `between` line with host >> guest is host time the row spends on
   itself; with guest >> 0, the machine running unwatched.

8. **`UI.press(name, need=, done=)`, `UI.keys(*names)`, `UI.type(text)`**
   (os88ui): `key_taken` then `ui_done` - the verb for `m.key(k);
   settle(m)` (35+ sites) and `m.key(k); pace(.15); ui_done(m)`. `need=True`
   raises when the key never reached the BIOS ring. Worked example:
   tests/fmarrows.py (nine `settle`s -> `press`/`still`).

9. **`os88mouse` `_edge` polls every 5 ms** (was 20): a Microsoft packet is
   ~25 guest ms at 1200 baud, ~6 host ms on an idle box, so every proven
   button edge paid up to a 20 ms floor - two to four edges a click.

## Measurements

(after 09:05 UTC; see below)

## Next time, ranked by expected host time saved

1. Make the boot cache the DEFAULT once a soak has run with it on and the
   rows' results match: ~430 scripts boot through `os88ui.boot`.
2. The same cache for raw `launch(boot=False)` + `kernel_up` callers (~85
   call sites) - it needs the caller's `boot=` semantics kept, which
   `os88ui.boot` already pins.
3. `_ui_settled`'s `ok()` and `ui_done`'s hold loop as one batch (disk,
   ticks, ui_lastt and the idle bytes): ~8 round trips a look today.
4. A server-side `wait` verb (86Box has `wait_mem`): the poll loops then cost
   one round trip a wait rather than one per look.
