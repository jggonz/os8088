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
   declared wall from the kill), excitebikeref 600 -> 1500 (1.25x),
   waverackedit 900 -> 1500 (it measured 430 s at the load below). Set from
   the measured walls, not raised: waverackrec* and waverackdisk (below).

8. **waverackplay counts laps, and its play watch is guest-timed** (goal 2):
   the loop leg's host sleep of 2.6 laps' time, which a loaded box turned
   into fewer than the two laps it checks, is a wait for verb 9's
   `wt_played` to pass 2.6 laps; `play_to_end`'s 60 s watch is a
   `os88qemu.Clock`.
9. **os88soak's preflight on a shallow clone** (goal 3): found here too,
   and fixed by the office stream's 10b460b (cherry-picked; this stream's
   own version reverted so the two do not conflict).

## Measured

One scoped soak, `os88soak.py start --marty-jobs 1` over the ten rows the
changes reach, 09:05 UTC, **10/10 ok** in 30:41 - on a box carrying 9 to 13
other emulators of the other streams the whole time (`status`'s in-flight
list), so every wall below is a LOADED wall. Beside it, `.scratch-agent/ab.sh`
ran the base commit's scripts (a symlinked root with the old files) for the
A/B. Walls are host seconds; QEMU rows have no WAITLOG.

| row | declared | before | after | note |
|---|---|---|---|---|
| waverackrec (5 rows) | 2,400 one row | - (never measured) | 226 + 149 + 59 + 65 + 14.5 = **514 s**, longest leg **226 s** | guest time unchanged by the split: 743 guest s of waits (OS88_WAITLOG); the critical path at lane width >= 2 is the hdd leg |
| waverackdisk | 1,500 | 266.8 (WAVERACK-PLAN.md's final run, idle) | **320.8 s** | 742 guest s of waits; secs 400 |
| xtmp3eng covox leg | - | **60 s** (A/B) | **13 s** (A/B) | the old 45 s sleep let the WHOLE 111 KB file play (111,517 writes); the wait stops at 30,317, the same check passing (30,312 identical from the head) |
| xtmp3eng | 750 | - | **273.5 s** | secs 320; per-leg host seconds now printed |
| waverackedit | 420 | **490 s** (A/B) | **432 s** (A/B), 430.1 (soak) | -12% from the key pauses alone |
| waverack | 150 | **191 s** (A/B) | **186.9 s** | stability only: the guest-budgeted `until` |
| waverackplay | 480 | **105 s** (A/B) | **101.6 s** | stability only: the lap count, the guest-timed watch |

`secs=` re-declared to these walls: waverackrec 300 / -clu 200 / -fd 90 /
-calls 90 / -q 30, waverackdisk 400, waverack 190, waverackplay 130,
waverackedit 440, xtmp3eng 320 - **5,700 declared seconds become 2,190** for
the same rows (the scheduler packs lanes by `secs`).

Not run, and why: `xtmp3rate` needs `qemuvt` (build/qemu9, not built here)
and would SKIP - its script change is the per-leg print and the lock;
`xtmp3live` imports xtmp3eng but reaches neither the covox helper nor
`main`; gbaot and excitebikeref changed only their timeouts.

Break-it: no row's ASSERTION changed. The covox wait ends on the same
quantity the check then tests, so a Covox given nothing still waits out its
240 s and goes red on "at least two seconds"; `ui_done` is capped at each old
pause, so a key that is never taken spends exactly the old pause and the
check after it is what it was.

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
