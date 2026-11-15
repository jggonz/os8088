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
     emulator binary (`$OS88_MARTY_BIN` when set) and its `configs/` and
     `media/roms/` (path, size,
     mtime), the gate card, the saver choice. Not cached (`eligible` says why
     and the row boots fresh): any `launch` keyword but `label`/`timeout`
     (`extra`, `rom`, `run_dir`, `addr`, `detach`, a `boot=` number),
     `$OS88_ROM`, `$MARTYPC_WAV`, `$OS88_MEGA`, a non-Marty
     emulator, and a machine whose name says it has a disk or device that is
     written THROUGH rather than held in RAM, or a device writing a host
     file (`hdd`, `ide`, PicoMEM, NIC, `lpt`) - a fork does not roll a VHD
     back.
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

   **`os88marty.launch(..., boot="up")`** is the same cache for a raw
   launch: `launch(boot=False)` + `m.run()` + `kernel_up(m)` as one call,
   which is the shape ~85 rows open with and the one spelling a cache can
   serve without guessing what the row does next (a row arming a boot-time
   breakpoint keeps `boot=False` and is never touched). Converted as worked
   examples: multiseg, rehome, rehomeabort, mediadisk, drvup.

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

   `ui_done`'s confirmation look (`_ui_settled`, stepped on a paused guest
   up to 24 times) is one batch too - the read count, [ticks], [ui_lastt]
   and the idle spans: eight round trips a look before (`_ui_settled1`;
   checked equal to the serial look on 6,000 random memory states).

5. **TCP_NODELAY on the client socket too** (SOAK-SPEED 17; the server
   already set it). A request is one write, so this is insurance against a
   thread writing between a request and its reply.

6. **The greeting `ping` sets `_pipeline`**, so a Marty's first `batch` is
   not preceded by a second ping. **`Marty.restore` polls 10 ms** for the restored machine (was 100):
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

10. **`key_taken`'s ui_task look (`_ui_asleep`) is one batch**, and
    **`pace`'s nap never outlasts what is left at the idle rate** (1-10 ms,
    was a flat 10): a mouse packet's 0.054-guest-second pace overshot by up
    to its whole nap.

    `guest_sleep` naps the same way (still()'s 0.05 guest s paid a whole
    20 ms poll), and `menu_pick`'s wait for a menu bar to carry the item
    polls 50 ms (was 200).

11. **The debug server's hex encoder is a table, not a `format!` a byte**
    (`tools/martypc/debug_server.rs` `hex_encode`): every `read`, `fbuf`,
    `planes` and `vram` answer is hex, and a VGA frame is 921,600 bytes.
    Same output byte for byte. **And `fbuf` takes `"digest": true`** (FNV-1a
    64 of the frame, `fbuf_digest` in `ping`): `settle`'s captures with no
    gate (`_Screen(..., digest=True)`) compare 16 hex digits rather than two
    1.8 MB hex frames; the bands, if a diagnostic asks, are read from a
    fresh frame. `OS88_FBUF_DIGEST=0` is the A/B; an older server answers as
    before. **Needs the emulator rebuilt** (`tools/martypc/build.sh` /
    `make marty`; incremental, ~15 s) - until then the client falls back.

12. **The debug server answers a talking client within a FINE batch**
    (`run_loop`): a request waits for the batch in flight, a 60th of a guest
    second (3.7 host ms idle, ~14 loaded), so a running machine's round trip
    averaged 5.6 host ms - cProfile of fmarrows: 11 of 19 host s in
    `recv_into`. While a command arrived within the last 100 ms the batch is
    a tenth of that with `MARTYPC_DEBUG_FINE=10`; a machine nobody is
    asking runs whole batches. **OFF BY DEFAULT, because it is NOT invisible
    to the guest**: with it on, fdlgchoose (an Enter in Save As never closed
    the chooser) and wimgtrip (the bar never carried Disk) failed 3 runs in
    3, and passed with it off (`MARTYPC_DEBUG_FINE=1`) on the same binary.
    The likely mechanism is input timing - a `key` with down and up, or a
    mouse packet, is applied between batches, so a finer batch shortens
    what the guest sees of it - not proven. Measured with it on (fmarrows,
    loaded box): `recv_into` 9.9 -> 4.1 host s, the row 17.7 -> 15.4 host s.
    Worth finishing: hold a key's up for a whole coarse batch's worth of
    cycles server-side, then default it on.

13. **`Marty.vram` unpacks by table, `_Screen._band` counts by slice**:
    `vram` was a Python generator step a PIXEL (128,000 a CGA frame, 250,560
    a Hercules one) and every `settle` capture on a 1bpp card is one -
    cProfile of fmarrows at the base: 3.6 M genexpr calls, 1.9 host s in 28
    `vram`s. Now 0.5 / 0.8 ms against 9.8 / 19.9 (CGA / Herc, outputs
    checked equal on random frames). The VGA boot gate's band count
    likewise (equal on random frames).

## Measurements (2026-11-15, a box at load 5-9 with seven other agents)

Wall time on this box is noise-dominated; the interleaved A/B below (base
and new alternated row by row, two rounds) is the fair one. "new" = this
branch + OS88_BOOTCACHE=1 + the rebuilt server (fine batches off).

| row | base host s | new host s | note |
|---|---|---|---|
| fdlggrey | 10.4, 10.2 | 5.4, 4.7 | os88ui.boot: cached |
| fmarrows | 9.7, 9.4 | 5.6, 6.2 | cached + `press` |
| dispclose | 13.2, 12.8 | 13.0, 11.8 | raw launch: not cached |
| drvup | 15.3, 14.9 | 14.9, 15.0 | `os8088_xt_hdd`: not cacheable |
| dockmark | 21.0, 21.2 | 19.5, 20.1 | raw launch: not cached |

- `bootcache check` (5150 CGA, 360KB pair): cached and fresh boots agree
  on desk_rows, menu_nbar, spl_live, ss_idle, the mouse cells, dsk_vtab,
  mem_top and the rendered frame's sha1. A miss boots in ~5-7 host s; a
  hit is 0.03-0.04 host s.
- fmarrows under cProfile: base 28.5 s (13.7 of it a cold os88sym map),
  8.3 s in `recv_into`; final 6.0 s, 3.5 in `recv_into`. Waitlog: the boot
  gate (14 guest s) gone; 8.3 guest s waited.
- Scoped soaks, `--marty-jobs 1`: 25 rows (martyconc uilayer fmarrows
  multiseg rehome rehomeabort mediadisk drvup evqfull drvscroll minexflag
  heapscrl diskclone dockmark dockmodule dispclose dispdrag lzmod fdlggrey
  fdlgup fdlgchoose sbar vbtmenu wimgtrip editmove) all green on the
  deployed server without the cache, and all green with cache + rebuilt
  server. Later, with the vram table: those minus a few plus dispherc1 and
  alertanim (Hercules settles) green both ways.
- FAILED and fixed: with the server's fine batches ON, fdlgchoose and
  wimgtrip failed 3 in 3 and uilayer once (item 12): off by default now.
- The `between` lines on 25 rows: 757 gaps, 538 guest s, 154 host s - the
  machine running while the rows and harness do their own work. That is
  the next work list (`os88marty.py waitlog build/...wl`).

## Next time, ranked by expected host time saved

1. **Rebuild the emulator** (`tools/martypc/build.sh`) so items 11 and 13's
   server half are live; until then the client falls back silently.
2. **Make the boot cache the DEFAULT** once a wider soak has run with it on:
   ~430 scripts boot through `os88ui.boot`, ~4-5 host s each on a hit.
3. **Move raw-launch rows to `boot="up"`** where their first act is a verb
   or a read (dispclose, dockmark and most of the 222 `kernel_up` callers
   open `launch(...)` with a picture settle or `boot=False` + a different
   shape): each one is then cacheable.
4. **Finish the fine batch** (item 12): apply a `key`'s release a coarse
   batch's worth of cycles after its press, server-side, and check the
   failing pair green; `recv_into` halves.
5. The `between` gaps (538 guest s on 25 rows): per call site, which rows
   let the machine run while they compute.
6. `key_taken`'s ring read and `ui_done`'s two `disk` calls a pass into the
   batches; a server-side `wait` verb (86Box has `wait_mem`) would make a
   poll loop one round trip a wait.
