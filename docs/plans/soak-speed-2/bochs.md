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

The lane: `os88test._default_bpc` divides `BOCHS_PER_CORE` by the square
root of the rate - 2 a core at 4 - because a row's CPU does not scale with
the rate: the guest's instructions are the same at any rate and the
harness's verbs are a fixed count, only the host-time polls shrink.
Measured (wait4 rusage of the row's process tree, Bochs included, Xvfb
not): a row's CPU per RUN fell at rate 4 (sheetbtn 7.4 -> 6.7 s, uilayer
8.3 -> 6.6, sheetfmt 12.5 -> 10.0, czto 17.6 -> 13.3) while its CPU per
WALL second rose 1.6-2x (0.08-0.12 of a core -> 0.12-0.19). os88soak's
`_bpc` now asks `_default_bpc` (one line). `--bochs-per-core` overrides.

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

### 5. Box.guest_sleep's poll scaled to the rate

A standalone row's `guest_sleep` overshoots by a poll's worth of RUNNING
guest; the poll is divided by the box's rate (a no-op at rate 1, which is
every standalone Box unless `$OS88_BOCHS_RATE` is set).

### 6. Marty.lin (goal 3, parity)

`os88marty.Marty.lin(seg, off)` = `(seg << 4) + off`, the same call the
kern_mega shims answer for a selector (os88megacommon.MegaMixin.lin), so a
row can spell `m.lin(seg, off)` on every emulator. ~40 row scripts carry
`m.lin(...) if bochs else seg * 16 + off` or `getattr(m, "lin", ...)`;
every one of those spellings gives the same answer with the method there
(checked: none uses `hasattr(m, "lin")` to mean "is kern_mega"), and they
are left for their owners to simplify.

### 7. tests/czto.py: not started is not finished (goal 2)

Found by this pass's soak: `mega-czto` failed 1 of 5 at rate 4 on a box
at load 10-13 - `hopfail` returned `None after 0 failed reads`, and the
join then ran into the NEXT leg's breakpoint ("the GUEST CLOCK HAS NOT
MOVED ... 'breakpoint' at 0F38:4061"). `marginal()` read "no claim and
mode 0" as the join's END, which is also the moment before it begins (the
Enter that starts it still on its way). The end is now believed only once
the join has been seen (its claim, or a stop of its own); one that never
starts answers `NEVER STARTED` after ~27 guest seconds instead of looping.
A row race at any rate - the host-time gap between `unto`'s Enter and the
first look is what loses it. Broken on purpose (no Enter for HOP.001):
`hopfail BAD 'NEVER STARTED' after 0 failed reads`, rc 1. Fixed: mega-czto
ok x1, czto (MartyPC) ok x1.

## Measured

Box at load 9-13 on 4 cores (eight agents), so wall times are noisy; the
legs of one row ran AT ONCE so they shared the load. `old` = rate 1,
`OS88_BOCHS_GLASS=sleep`, `OS88_BOCHS_NAP=0.02`, `OS88_BOCHS_CAPTURE=sleep`
(the tree before this pass); `new1` = this pass at rate 1; `new` = rate 4.
Host wall seconds, all green:

| row | old | new1 | new (rate 4) |
|---|---|---|---|
| mega-sheetbtn | 62.6 | 61.4 | 35.2 |
| mega-uilayer | 109.1 | 107.7 | 55.7 |
| mega-assocopen | 24.8 | 22.6 | 10.0 |
| mega-minesrc | 48.4 | 46.0 | 21.4 |
| mega-vidfskeysvga4 | 67.0 | 63.1 | 30.9 |
| mega-sheetfmt | 124.0 | 119.0 | 63.4 |
| mega-doscom | 42.6 | 40.3 | 14.2 |
| mega-czto | 195.0 | 192.1 | 82.6 |
| **sum** | **673.5** | **652.2** | **313.4** |

So the rate is the change (0.47x); the glass, the naps and the capture
are 3% between them. Guest seconds are the same by construction except
for the glass: a running look now spends 0.12 guest s where it spent up
to 0.45.

Higher rates (same three rows, legs at once): uilayer 58.6 / 46.6 / 44.0
s at rate 4 / 8 / 16, sheetfmt 66.7 / 47.6 / 42.0 - and
**mega-vidfskeysvga4 FAILED at 8 and at 16** ("3: the toast (frames 20
to 29) was not up when the block under it arrived (frame 30)"), green at
4 in 3 of 3 runs. Past 4 the harness's own host work is most of a row
(cProfile of mega-assocopen at 4: 10.1 s, 575 guest reads at ~6 ms each
are 3.3 s of it, the boot 2.1 s), and host-time races left in rows start
to show. 4 is the default; 8 is the next experiment once vidfskeys' leg 3
is understood.

The verification soak, `os88test.py --marty-jobs 1 soak` (bochs-per-core
2, the new default) over 29 mega rows covering every change - the glass
(vidfskeysvga4, pxfolder, sheetfmt), input (assocopen, minesrc, alertbtn,
fmarrows, altenter, kbdburst, ps2mouse; the serial mouse's paced edges:
irqgrab, kdmouse, dosmouevt), exact pacing (trkcompact on a 1MB machine,
stikmenu, weavemove), bp_trace pumps (bptrace, int0sweep, deskwhole),
DOS trips (doscom, dostype), floppy swaps (imgswap, czto), rate-1
machines (dossnd's SB16, ethernet's NE2000), toasts (toastfpg), cfgtrip,
uilayer: **28 passed, 1 failed (mega-czto, item 7) in 1000.5 s** against
2810 s declared. mega-czto then passed 4 of 4 at rate 4 and 2 of 2 at 1,
and once more after item 7.

`secs=` in tests/megaport.txt moved for the eight rows measured above and
czto, to ~1.2x the rate-4 figure under this load; the rest keep their old
figure, now generous.

## Left for the next pass (ranked by host time it could save)

1. **Batch the shim's reads.** At rate 4 a row is mostly harness: 517
   single `m.read`s in mega-assocopen are 3.3 of its 10 s, each a stop.
   `until`/`ui_idle` polls that read several words could take one
   `reads` stop; or the shim could coalesce reads issued while one stop
   is open.
2. **Rate 8**, once vidfskeys leg 3's host-time dependence is found (it is
   red at 8 and 16, green at 4): another ~20% on the rows tried.
3. **A booted-desktop snapshot** shared across rows: Bochs 2.7 restores
   with `bochs -r DIR`, but has no debugger command to SAVE, so it needs
   the GUI's suspend path driven - not attempted. The boot is ~2 host s at
   rate 4, so ~12 minutes over the 353 rows.
4. **`mem` breakpoints on Bochs** (`watch w`, which `Box.watch_write`
   already drives): the shim refuses them, which keeps dispcold,
   dispfreeze, dispreboot, kdreturn, paintrate, pxbench, pxfsx, uilat,
   vidplay off kern_mega or on workarounds.
5. The row scripts' `m.lin(...) if bochs else seg * 16 + off` spellings
   can become `m.lin(seg, off)` now (item 6).
