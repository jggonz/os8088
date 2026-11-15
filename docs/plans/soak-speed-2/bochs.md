# Soak speed, pass 2: the Bochs / kern_mega rows

The `mega-*` rows (353 of them, 8.4 h of declared `secs`; 400 rows need
Bochs, 9.7 h) are MartyPC row scripts run under Bochs through
`tools/os88megabochs.py` (docs/BOCHS.md, *Rows ported from MartyPC*). This
is the ledger of what this pass changed in that harness, why, and what it
measured. docs/plans/completed/SOAK-SPEED.md is the previous pass and its
numbered techniques are cited by number.

## Where a mega row's host time went

Read off the code before measuring, then checked (*Measured*, below):

1. **The guest was capped at REAL TIME.** `clock: sync=slowdown` with the
   clock preload (`tools/bochs/bxclock.c`) holds Bochs to one guest second
   a host second. Every guest-time wait of the shim - `pace`, `advance`,
   `settle`'s windows (4.5 guest s for `pace(1.0)`), `ui_done`, the boot's
   ~6 guest s - cost its full length in host seconds, where MartyPC runs
   the same script's guest at ~3.4x. So a mega row was a guest-bound row
   held below the speed its host could run it: the second-largest emulator
   cost of the soak for that reason above all.
2. **Every look at a RUNNING guest slept `GLASS`**, 0.45 host s, for the X
   window to catch up with Bochs's realtime VGA timer.
3. **Every debugger round trip polled e9.out with naps doubling to 20 ms**,
   on each of a stop's three waits (the prompt, the commands' prompts, the
   `c`'s echo) - a stop is ~20 ms of Bochs, so the naps could double it.
4. A fresh display's Xvfb was given a fixed 0.5 s after its socket
   appeared.

## What changed

### 1. The cap's RATE (goal 1, and goal 2)

The cap is the preload's, so it can move: `bxclock.c`'s `time()` now runs
`$OS88_BXCLOCK_RATE` times the host's RUNNING time (a stop is still taken
out whole), and `slowdown` then holds the guest to that many times real
time. `os88bochs.Box(rate=)`; a row's machine (`os88megabochs.launch`)
takes `os88bochs.ROW_RATE` (default `ROW_RATE_DEFAULT`), everything else 1;
`$OS88_BOCHS_RATE` sets all of them, and `OS88_BOCHS_RATE=1` is the A/B.

Why a rate and not `sync=none`: the same machine either way - 4M
instructions and 18.2 ticks a GUEST second, so no verdict a row reaches on
the guest's clock can move - but `none` is uncapped, so a row's CPU would
be whatever the host lets it take, and the lane's weighting (os88test
`BOCHS_PER_CORE`) and the handful of host-second waits that remain
(`Box.until`'s and `up`'s budgets, `ui_done`'s 0.25 s hold, `INPUT_GAP`)
would have no ceiling to be sized against. A rate keeps one, and it is
also headroom: a loaded box that gives a rate-4 guest a third of what it
asks still runs it FASTER than real time, where at rate 1 it fell below.

Two exceptions keep rate 1 unless `$OS88_BOCHS_RATE` says otherwise: a
machine with an NE2000 (its slirp, and whatever answers it on the host -
os88proxy, a host curl - keep host time) and one with an SB16 (the
waveout mixer thread drains on host time, iodev/sound/soundlow.cc).

Past real time Bochs's VGA update timer would still run on the host's
clock (Bochs 2.7's `vga: realtime=1` default), so the glass would lag the
guest RATE times as far: a machine with a rate above 1 gets
`realtime=0`, the guest's clock.

The lane: `os88test._default_bpc` divides `BOCHS_PER_CORE` by the rate
(os88soak's `_bpc` now asks it), so the runner never admits more guest
seconds per host second than it did. `--bochs-per-core` still overrides.

### 2. The glass on the guest's clock (goal 1)

`MegaBochs._fbuf` on a RUNNING guest runs it exactly `GLASS_GUEST` (0.12)
guest seconds on a time breakpoint (`_run_exact`), two of the shim's 20 Hz
VGA updates and a margin, dumps the window with the guest STOPPED, and
resumes it. Where no exact run is possible (a breakpoint of the row's own,
a `bp_trace` pump, `realtime`) it sleeps `GLASS / rate`.
`$OS88_BOCHS_GLASS=sleep` is the A/B.

### 3. The debugger's naps (goal 1, SOAK-SPEED 17's question)

`os88bochs.NAP_MAX`, 5 ms (was 20 ms), caps every e9.out / bochs.log poll
of the shim and of `os88bochs.session`. `$OS88_BOCHS_NAP=0.02` is the A/B.

### 4. Xvfb up when it ACCEPTS

`os88bochs.xvfb` connects to the socket until the server accepts, instead
of sleeping 0.5 s after the socket file appears.

## Measured

(filled in below)

## Left for the next pass

(filled in below)
