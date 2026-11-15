# Soak speed 2 - the second soak-speed and stability pass

The second pass after [SOAK-SPEED.md](../completed/SOAK-SPEED.md), run as
eight parallel streams on 2026-11-15, each in its own worktree, merged onto
one branch. Its four goals:

1. **Less host time per row**: fewer host waits, less guest time, and boots
   and legs batched together.
2. **Stability**: rows that flake, or could flake, on host timing.
3. **Harness parity**, so a row has fewer things to work around, with the
   emulators as close to one another as they can be.
4. **A soak runs on a frozen copy of everything**, so a session can commit,
   `make` and rebuild while its own soak runs.

Each stream's ledger is the record of what it changed, what it measured and
what it left for next time:

| stream | goal | ledger | headline |
|---|---|---|---|
| frozen | 4 | [frozen.md](frozen.md) | `os88soak.py start` runs inside a detached worktree snapshot of the whole checkout (SOAK-PARALLEL.md 14.4); proved under live sabotage |
| core | 1, 3 | [core.md](core.md) | MartyPC harness: a boot cache, on by default (`OS88_BOOTCACHE=0` turns it off), `launch(boot="up")`, batched UI-idle reads, an fbuf digest, `press`/`keys`/`type` |
| bochs | 1, 2, 3 | [bochs.md](bochs.md) | kern_mega rows run at 4x real time on the same guest clock (`OS88_BOCHS_RATE`), about half the host time |
| qemubox | 1, 2, 3 | [qemubox.md](qemubox.md) | QEMU rows on the guest's clock with closed-loop mice; `OS88_EMU=qemu` runs an os88ui row on QEMU |
| vid | 1 | [vid.md](vid.md) | 74 `vid*` rows run as 25 rows that share a boot (`tests/vidshare.py`) |
| office | 1, 3 | [office.md](office.md) | sheet and chart rows wait on state; `SF.menu` confirms the press on `sh_mopen`; a `pkg_syms` cache |
| games | 1, 2 | [games.md](games.md) | waverackrec split into five rows, `locks=("script:private",)`, WaveRack on the guest's clock |
| flake | 2 | [flake.md](flake.md) | the `t_hostwait` ratchet (`tests/hostwait.txt`), `os88deadline.Guest`, a TIMEOUT line that names its cause |

**At a merge**, run `python3 tests/unit/t_hostwait.py --update` after the
last stream lands, so the ratchet records the counts the streams lowered.
