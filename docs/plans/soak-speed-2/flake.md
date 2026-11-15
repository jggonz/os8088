# Soak speed 2 - the `flake` stream: rows that go red under HOST load

Stream goal 2 of the soak-speed pass (docs/plans/completed/SOAK-SPEED.md is
the first pass and its numbered techniques): a row that passes alone and
fails in a loaded soak because something in it waits in HOST seconds for
GUEST work, or shares a host resource with a concurrent run.

## What the harness already does (read before adding a mechanism)

- `os88marty.until`, `settle` and `quiesce` charge their budgets on the
  GUEST's clock (`_Progress`, `GUEST_BUDGET_RATIO`), so their `limit=` /
  `quiet=` arguments are not a flake source. `launch(boot=N)` and
  `click(settle=N)` pace in guest time too (`pace`). What is left is the
  code rows write THEMSELVES round those calls.
- `os88test.py`'s row timeout is charged in the row's CPU (process tree,
  wait4), with the wall clock a backstop 5x wider (`WALL_BACKSTOP`); QEMU
  rows are charged through their pidfiles. A queued row is not timed out
  for being queued - only a row that spins, or one stuck past the backstop.
- Fixed `build/...` reads under a frozen tree are tests/unit/t_artpath.py's.

## 1. The audit (static, 2026-11-15)

`python3 tests/unit/t_hostwait.py --list` / `--rank` is the live form of
this section; the kinds are its docstring. Census of 1,162 test scripts
before this stream's fixes:

| kind | sites | flakes? |
|---|---:|---|
| sleep (not a poll interval, >= 0.1 s, unmarked) | 88 | yes |
| deadline (`time.time()/monotonic()` + N, or in a comparison) | 117 | yes |
| port (fixed TCP port literal) | 0 | yes (but see 3: named constants) |
| tmppath (`/tmp/...`, `build/qmp.sock` literal) | 3 | yes |
| bootpace (`launch(boot=N)`) | 5 | no - guest time, waste |
| clickpace (`click(settle=N)`) | 5 | no - guest time, waste |
| poll (`poll=` > 0.05) | 631 | no - a host floor per wait |
| advance (`advance(frames=N)`) | 560 | no - blind guest wait |
| passwait (`required=False` / `need=False`) | 49 | no - budget paid on green |

Ranked by (declared row secs x flaky sites), the top of the list and whose
stream owns each (`--rank` BEFORE the fixes below):

| score | file | flaky sites | owner |
|---:|---|---|---|
| 8820 | waverackedit.py | sleep 21 | media stream |
| 8160 | waverackplay.py | sleep 16, deadline 1 | media stream |
| 3510 | telansi.py | deadline 4, sleep 5 | qemubox stream (note below) |
| 3435 | xtmp3eng.py | sleep 3 | media stream |
| 2400 | waverackrec.py | sleep 1 | media stream |
| 2400 | win16pbrush.py | sleep 9, deadline 1 | flake: NOT fixed (SKIPs here - no PBRUSH.EXE) |
| 2000 | hibernate.py | deadline 4 | **flake: marked (host file flush)** |
| 1920 | telzm.py | sleep 4 | qemubox stream (note below) |
| 1800 | waverack.py | deadline 3, sleep 9 | media stream |
| 1300 | vidxip.py | deadline 1 | vid stream |
| 912 | trkspk.py | deadline 2 | **flake: fixed** |
| 600 | pxbench.py | deadline 1 | games stream |
| 600 | win16write.py | sleep 2 | flake: NOT fixed (SKIPs here) |
| 520 | smallboot.py | sleep 1 (+bootpace) | **flake: fixed** |
| 500 | rdgaotmega.py | deadline 2 | mega stream |
| 450 | vesabench.py | deadline 2, sleep 1 | flake: left (QEMU; mtools poll of the guest's file) |
| 420 | wirechat.py | deadline 2 | qemubox stream (genuine host: the server's long-poll hold) |
| 396 | apspk.py | deadline 2 | **flake: fixed** |
| 340 | chuge.py | deadline 4 | flake (big leg) / mega (mega leg): left |
| 300 | lemmings.py | deadline 2, sleep 1 | flake: left (QEMU dump poll) |
| 240 | hdbounce.py | deadline 4 | **flake: fixed** |
| 215 | brnav.py | sleep 1 | qemubox stream (genuine host: the server's hold is SPEC.md 71.8's subject) |
| 90  | brrace.py | deadline 1 | **flake: fixed** |
| ~40 files | mega*.py | deadline 2-4 each | mega stream (one shared shape) |

The disp*/dock*/desk*/multiseg/pkgmove/tm*/dos*/assoc* families named in
the brief are already clean of the flaky kinds except four stall detectors
(dispreboot, dispcold, dotdelpen: "the cycle count has not moved for N host
seconds" - a genuine host question, now marked) and dosfcbrd/dosfcbwr's
`boot=30`, which is guest time and only in their `--ref` (IBM DOS) path.

## 2. The ratchet: tests/unit/t_hostwait.py + tests/hostwait.txt

One line per (file, kind) with a reason; a count above its line fails (a new
host wait), a count below fails too (a stale line), and
`python3 tests/unit/t_hostwait.py --update` is the one command after any
fix - it lowers and drops lines and never raises one; `--raise "<why>"`
does. A genuine host wait is marked `# host: <why>` on its line or the line
above. AST-walked, so comments and docstrings never count. Registered in
SOAK (`hostwait`): fast/full need the owner's line in tests/tierlock.txt,
and it is ~3 s idle (~7 s at a load of 30) - 16 MB of Python to parse.
Verified red: a `time.sleep(2)` after a click in dispcp.py, a removed
`# host:` mark in hibernate.py, a line left one high.

**Merging:** other streams lower counts in their files concurrently; after
the merge run `python3 tests/unit/t_hostwait.py --update` once.

## 3. Fixes in this stream's rows

- **tools/os88deadline.py `Guest(m, secs)`** - a poll budget on the guest's
  clock for the loop shapes `until` does not fit (a breakpoint pump that
  also watches `m.stopped()`, a loop that collects as it goes). True while
  the guest has spent < `secs x GUEST_BUDGET_RATIO` of its own seconds
  (the same conversion `until(limit=)` uses), with a host backstop 10x out
  for a guest that stopped. Works on anything with `status()["cycles"]`
  (MartyPC, the 86Box shim).
- **brrace**: the click-to-`br_abort` window, 60 host s -> `Guest(m, 60)`.
- **apspk** (all five rows): the play's start (60) and its run (600).
- **trkspk** (all its rows): `pulses`' 600-s budget; and `writes(port, 0,
  limit)` - "how many writes to 42h in 10 s", the covox leg's "speaker
  untouched" proof - was a WALL window, so a loaded soak watched a fifth of
  the guest time an idle box did and proved less. Now `limit x GUEST_PACE`
  guest seconds: the window an idle box gave, on every box.
- **hdbounce** (86Box): finding MEGARD's image (120) and the counted read
  (600) on emulated time.
- **smallboot**: "the guest is still executing" was cycles across a 0.4-s
  host sleep; at a load of 40 that sleep can see the emulator descheduled
  for all of it. Now a poll up to 5 host s that ends at the first moved cycle.
- **Notes for the qemubox stream (QEMU network rows are theirs; edits
  reverted here)**: in telansi/telzm the seven `time.sleep(0.3)` after
  `srv.start()` buy nothing - os88bbs.BBSServer and telansi's MirrorServer
  bind and LISTEN in `__init__`, so the guest's connect queues on the
  backlog. Their bind retries and port-release pauses (0.6 s) are genuine
  host waits; telansi's MirrorServer 12-s read of the guest's reply is a
  host deadline on guest output. wirechat's 20-s hold and brnav's
  SLOW_SECS are genuine host waits (servers' holds) to mark `# host:`.
- Marked as genuine host waits: hibernate (the host FILE's flush),
  dispreboot/dispcold/dotdelpen (stall detectors),
  heapmap/stikhiber (10x backstops on guest windows), vbthold (glass sample
  spacing), vbtdiag (a debug-only print loop).

## 4. Runner level (tools/os88test.py, additive)

- **A TIMEOUT SAYS WHICH LIMIT FIRED** (`_timeout_note`). The CPU limit
  ("CPU-bound (N CPU s > T)": the row did that much work - a spinning hang
  or a row slower than declared; load cannot cause it) and the wall
  backstop, which is split by the **run queue**: the row's live process
  tree's `/proc/<pid>/task/*/schedstat` RUNNABLE-waiting time. More than a
  quarter of the wall spent queued is `HOST-STARVED`; little queued is
  `blocked/hung` (a socket, a lock, a stopped machine). Load alone cannot
  tell those apart - a sleeping row on a box at load 30 is still hung - so
  load is printed but not the witness. Tested on a sleeping child
  (`0 s queued ... blocked/hung`) and a `nice 19` busy child at load 43
  (`2 s queued ... HOST-STARVED`). Nothing changes WHEN a row is stopped.
- **`port:N` locks are taken across runners** (`_port_hold`): an flock on
  `/tmp/.os88port-N.lock` for the row's life, the `_bochs_take` shape.
  `lane_locks` already kept two rows of one runner apart; with eight
  agents' soaks on one box, ethernet / brdownload* (8090), telansi (8094)
  and the dos*cable family (8099) in two worktrees bound one port at once.
  A held port is waited for (up to 30 min, then the row runs and says so).
- Not locked by anything: tests/socktest.py binds 127.0.0.1:8088 (SK_PORT
  in its .asm) with no `locks=`; its script lock covers one runner only.

## 5. Left for the next pass (ranked by expected host time / flake risk)

1. waverack* (media stream): 47 host sleeps - the biggest flake mass.
2. The mega*.py `deadline` shape (~40 files, 2 sites each): one helper
   (`os88deadline.Guest` works on the Bochs shim's status) converts them.
3. The 631 `poll=` > 0.05 sites: SOAK-SPEED 8's floor, per wait, on every
   green run; `poll=0.5`/`1.0` (130 sites) first.
4. The 560 `advance(frames=N)` blind waits (skies*: ~150) - SOAK-SPEED 4.
5. win16pbrush / win16write: QEMU host sleeps between mouse steps; they
   SKIP without the reference samples, so convert where they can be run.
6. Telnet's host deadlines (telansi's 12-s MirrorServer read) are host
   threads reading guest output: a guest-tick budget needs the QMP handle
   threaded into the server.

## 6. Verification (after 09:05)

See the commit that follows this one for the soak results.
