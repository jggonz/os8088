# Kernel size pass 19 - `fix2`: the final soak's failures, second share

Branch `ksp19-fix2`, cut from `kernel-size-19` at `5b413f66`. Every row
here turned out to be a cause `fix` had already found (its note,
`docs/plans/completed/ksp19/fix.md`, "SHARED CAUSES") or a stale row; nothing in this
note changes a shipped byte of its own.

**Shared with `fix`**: this branch CHERRY-PICKS three of `ksp19-fix`'s
commits so the rows could be re-run here - `b1608e34` (the harness reads
W_SEG at `os88geom.W_SEG`, 38 tests), `155b23bb` (CLEAR SKIES' entry `clc`)
and `dd1e6e5c` (PiXEL's `px_claims` keeps CX). Same patches, so the merge of
both branches is clean; the files are `fix`'s.

## W_SEG and 11.96.3.1 - no product change

The coordinator's suspicion (wmrec made kernel windows' W_SEG 0) does not
hold: a kernel window's W_SEG was 0 before this pass too, and SPEC.md
11.96.3.1 already publishes "`[W_SEG]`, or `KERNEL_SEG` when that word is
0"; `wm_ownseg` answers exactly that. wmrec only biased a kernel window's
W_CLASS. What broke was R14 moving W_SEG 22 -> 18 under tests that read a
bare 22 (the next word: W_TIMER / W_PREF). No re-contract, no dispatch cost.

## Per row

| row | verdict | fix | bytes | re-run |
|---|---|---|---|---|
| dispcalc | stale row (W_SEG at 22: "package segment 0000", so the display was read out of segment 0) | `fix`'s b1608e34 | 0 | ok 81s |
| tmselfsu | stale row (W_SEG at 22: slot read 01EA = W_PREF) | `fix`'s b1608e34 | 0 | ok 93s |
| lzmod-lzb | stale row (`tests/lzmod.py` W_SEG at 22, so `[trk_modseg]` read in the wrong segment) | `fix`'s b1608e34 | 0 | ok 8s |
| brclick (added by the coordinator) | stale row (W_SEG at 22 -> a title read out of the wrong segment, UnicodeDecodeError) | `fix`'s b1608e34 | 0 | ok 75s |
| skiessound, skiesvga | PRODUCT: CLEAR SKIES' entry returned cs_set_load's CF = 1 once R14 dropped the CF-clearing setters (LD_EABORT) | `fix`'s 155b23bb | skies +1 | ok 32s / 36s |
| pxsave | PRODUCT: hung at "Next with an edit, Save" - the next picture's decode, PiXEL's CL clobber (fix's cause 4) | `fix`'s dd1e6e5c | pixel +2 | ok 218s |
| wirefleetsystem | stale row: zeroed the record's `W_ONCL`, gone with R14 | the forced-close leg zeroes the CLASS's `WC_ONCLOSE` in the package (`wf_cls`), fb797b67 | 0 | ok 59s |

The coordinator's "one root cause, every run-time claim fails" (Skies,
Tracker): neither reproduced as a claim failure here - Tracker's was the
W_SEG read, Skies' the entry CF.

Builds: `make` (fast tier 78 passed; over its 30s budget by 1.3s under the
soak's load, not a row failure). No SDK, kernel or package byte moved by
this branch's own commits, so no small/emu/mega/C rebuild, no abilock.
