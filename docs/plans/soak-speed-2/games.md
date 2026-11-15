# Soak speed, pass 2: the game and media rows

The stream: the long game and media rows - WaveRack, XTMP3, PiXEL, isoTron,
Excitebike, Pixelstein, zrestart, gbaot, install, lzbig, MIDIRack, WireBall,
ARCADE - 131 soak rows and about 5.3 hours of declared `secs`, many of them
single rows of 10 to 40 minutes. The toolbox is
docs/plans/completed/SOAK-SPEED.md's numbered techniques, cited by number.

## Where the time goes, read before anything was changed

The long rows split three ways, and the split decides what can be done:

| row | declared | where the time goes | lever |
|---|---|---|---|
| waverackrec | 2,400 s | GUEST: four 60 s takes, a 204 s take beside a second WaveRack's 2 MB scan, a 30 s floppy take and a disk-full one, a recovery boot, a calls leg - five machines, each its own boot, nothing shared but a 2 MB fixture | the legs are independent: split into rows |
| waverackdisk | 1,500 s | GUEST: 30 s plays and a 78 s scan. Its BEFORE build is never reachable (WaveRack came as one squash, #311), so only the after half runs; the plan's final run was 266.8 s | re-declare |
| pxdecode | 900 s | GUEST: ~450 opens through File > Revert. Host side measured: `pixcorpus.corpus()` 0.66 s, every `pixelsim` decode + emit 0.6 s | none safe: SOAK-SPEED's poll 0.3 -> 0.05 made it fail 2 in 5 |
| xtmp3eng | 750 s | 20 QEMU behaviour legs; the covox leg a 45 s HOST sleep | the sleep, and a lock |
| gbaot | 715 s | HOST CPU under Unicorn | timeout only (parallel ROMs: an idea, below) |
| lzbig | 400 s | GUEST compresses. Host side measured: every `lzb_compress_machine` fixture 4 s in all | none |
| excitebikeref | 480 s | GUEST: 29,465 steps in 256-step chunks | timeout only |

The MartyPC rows of this stream were already guest-timed by the first pass:
the only `time.sleep` calls left in them are poll naps inside guest-budgeted
loops. The QEMU rows were not - the WaveRack family waited in host seconds
throughout.

## Changes

1. **waverackrec is five rows, one a leg** (goal 1). `waverackrec` (hdd),
   `waverackrec-clu` (bigclu), `waverackrec-fd` (floppy),
   `waverackrec-calls` (calls) and `waverackrec-q` (noinput, QEMU). Each leg
   boots its own machine, so nothing is lost; the QEMU leg leaves a row that
   needed both emulators. `-k 'waverackrec*'` is the old row.
2. **`locks=("script:private",)`** (goal 1, tools/os88test.py `lane_locks`,
   additive). The lane holds a row's SCRIPT as an implicit lock, which would
   run the five legs end to end again; a row may now say its script's scratch
   is private (mkdtemp, `os88marty.launch`'s run dir) and lift it. Declared
   on the five waverackrec rows and on `xtmp3eng` / `xtmp3rate` (every `Rig`
   a mkdtemp, the two fixed-looking paths pid-tagged), after checking each
   script for a fixed path.
3. **The WaveRack QEMU family waits in GUEST seconds** (goal 2, SOAK-SPEED
   11/12). `tests/waverack.py`'s `until` - shared by waverack, waverackplay,
   waverackedit and waverackrec's noinput leg - budgeted host seconds, so a
   10 s wait for a menu was a fraction of that on a loaded box. It is now
   `os88qemu.acted` on the QEMU each WR/PR/ED registers with `clock()`,
   falling back to host seconds only when no machine answers.
4. **waverackedit's key pauses end when the UI is done** (goals 1 and 2,
   SOAK-SPEED 4): `keyed`'s 1.2 s and the legs' pauses after a key or a state
   wait are `os88qemu.ui_done`, capped at the pause each replaces. The pauses
   after MOUSE gestures stay: a packet still in the serial line is invisible
   to `ui_idle`.
5. **xtmp3eng's covox leg waits for the samples** (goals 1 and 2): a fixed
   45 s host sleep sized for ~31,000 samples at ~700 a wall second idle -
   and fewer than the two seconds it checks on a loaded box. It waits for the
   data-port writes in QEMU's own trace instead (2 s + 2,048 since the
   moment, an undercount), 240 s at most. Every leg now prints its host
   seconds, which is what a split of xtmp3eng would be decided on.
6. **waverackdisk's `guest_wait` polls at 0.05** (goal 1): 0.5 host s a
   look overran every wait by up to ~1.7 guest s (waverackrec imports it).
7. **Timeouts** (goal 2): gbaot 900 -> 2400 (a CPU-bound host row 1.26x its
   idle wall from the kill), excitebikeref 600 -> 1500 (1.25x),
   waverackplay 900 -> 1800 (1.88x).

8. **waverackplay counts laps, and its play watch is guest-timed** (goal 2):
   the loop leg's host sleep of 2.6 laps' time, which a loaded box turned
   into fewer than the two laps it checks, is a wait for verb 9's
   `wt_played` to pass 2.6 laps; `play_to_end`'s 60 s watch is a
   `os88qemu.Clock`.
9. **os88soak's preflight on a shallow clone** (goal 3): `_buildnum_current`
   compared `build/buildnum.inc` with `git rev-list --count`, but
   tools/buildnum.py writes 0 ON PURPOSE on a shallow clone - so every
   fresh container's `check` said MISS "make has not run since the last
   commit" right after a make. It now also accepts what `buildnum.py` itself
   would write.

## Measured

(filled in by the verification pass after 09:05 UTC)

## Ideas for the next pass, by expected host time saved

1. **waverackrec's take lengths** (~120 guest s): the four 60 s takes could
   be 30 s and still span 10-20 writes each, but "60 s, 0 overruns" is the
   sustained-record figure WAVERACK-PLAN.md US-11 quotes - the owner's call.
2. **gbaot across processes** (~2/3 of 715 s on an idle box): every ROM is
   translated and compared independently; a `multiprocessing` pool with
   `cpus=3` would cut its wall where cores are free, and nothing where not.
3. **xtmp3eng split in two** once its per-leg times (change 5) are in a log.
4. **pxdecode's lost Ctrl+R** (SOAK-SPEED's note): until the cause of a key
   ignored just after a refused open is known, its polls cannot drop.
