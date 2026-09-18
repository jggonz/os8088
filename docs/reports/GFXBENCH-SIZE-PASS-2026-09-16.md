# GFXBENCH / SYSBENCH across the size pass on this cycle's kernel additions

**Taken 2026-09-16.** Base `2520c019` (the `elendilon-next` tip the pass was
cut from) against `0a5e88cc` (`size-pass-kernel-additions`, all eight slices
in). MartyPC, cycle-accurate 4.77 MHz 8088, in the project container;
`os8088_5150_herc_gla` and `os8088_xt_vga`, 360 KB drives, `build/bench360.img`
in B:.

**This is a MEASUREMENT of those two trees and of no other** (docs/README.md).
It is not maintained; a later reading is a new file.

## The question

The pass took **1,820 resident bytes** off `kern_big` and rewrote enough
mechanism to be worth checking: `OSAPI_VOL_STAT` answers in registers where it
filled a record, `OSAPI_FILE_COPY`/`_MOVE` became one door, the compactor's
plan became one walk, and — the only change with a plausible *per-call* cost —
**forty API cells stopped going through a resident near thunk and now reach
their `.cold` body through one shared trampoline** (SPEC.md 20.3.2).

## What it says

**Nothing in the drawing layer moved.** 74 rows per adapter, one run each:

| adapter | rows | moved ≥1% | largest move |
|---|---:|---:|---|
| Hercules 720x348 | 74 | **0** | −0.22% `GFX_BLIT4 4px runs` (faster) |
| VGA 640x480 planar | 74 | **0** | −0.10% `ISA status port in` |

Every other row on both adapters is within **±0.02%**. One run an arm is enough
*because* of that: 148 rows agreeing to a fifth of a percent is itself the
reproducibility check — jitter would scatter them, not align them.

`gfxbench` prices the far-call floor (`GET_TICKS`, `SET_COLOR`, `GFX_PEN`,
`WM_*`, `MOUSE`), and **every one of those cells has a resident body, so none
of them was converted.** That is why `sysbench` was run too: its file and
memory rows go through `dwf_dskw_read`, `mmf_osapi_mem_*` and the volume
doors, which *are* in the converted set.

## sysbench — n=6 per arm, because four rows moved at n=1

41 rows. At one sample an arm, four rows read ≥1% apart. Six samples an arm
settle every one of them:

| row | base (n=6) | head (n=6) | verdict |
|---|---|---|---|
| `int 1Ah AH=00h` | 5032–5074 | 5033 ×3, 5098, 5155, 6537 | noise — same mode, and **the kernel does not hook `int 1Ah`**: it is ROM either way |
| `near call + ret` | 3841–3851 | 3849 ×5, 4134 | noise — head's mode is inside base's range |
| `read 16K, cold motor` | 19–22 ticks | 20–22 ticks | tick-timed, **one 55 ms quantum is the resolution**; rotational latency |
| `read 1 sector file` | 5–6 ticks | 5–6 ticks | same |
| `API far call cell` | 16722–16728 | 16726–16730 | **unchanged — the cell rework's own row** |
| `TASK_YIELD`, `FILE_HERE`, `work, interrupts on` | — | — | identical in all twelve runs |
| **`FILE_DFREE`** | **640985 ×5, 640984** | **639242 ×6** | **DISJOINT: −1,743 counts, −0.27%, FASTER** |

**`FILE_DFREE` is the only reproducible difference in either harness, and it is
an improvement** — `OSAPI_VOL_STAT` handing back four registers instead of
composing a 12-byte record no caller read (SPEC.md 18.4.6).

The two single-sample outliers are what a tick landing inside a short window
costs: `near call + ret` is 300 iterations of ~13 counts, so the window is
~3.3 ms against a 55 ms tick — about a 6% chance of catching one, which is
what 4134 is.

## Controls

* `build/sysbench.o88` and `build/gfxbench.o88` are **byte-identical** between
  the two trees, so the harness is not part of the comparison.
* The two arms ran the same machine definition and the same BIOS.
* The guest's figures are its own PIT readings under a cycle-accurate
  emulator, so they are exact at any host load; the arms were run in parallel
  deliberately.

## How to re-take it

There is no registered row for either harness — both are packages a person
drives. The drivers used here are in the session scratch and are twenty lines
each: boot with `build/bench360.img` in B:, `ui.path("B:/GFXBENCH.O88")`,
`menu_pick("Bench", "Run")`, `menu_pick("Bench", "Save Report")`, then
`os88flush.Flush(marty=ui.m).volume(1).read("GFXHERC.TXT")`.

Two traps, both paid for here:

1. **The package's menu set arrives after its window.** `ui.path` confirms the
   window; `OSAPI_MENU_SET` runs a moment later in the package's own entry
   proc, so under three emulators on four cores the first `menu_pick` finds
   the Disk window's bar. Retry it.
2. **`settle` cannot time `sysbench`.** Its own status line says the machine
   is frozen for the whole run, so the glass is still while it works and a
   settle returns into the middle of it. It **saves itself** when it finishes,
   so poll the floppy for `SYSBENCH.TXT` instead. `gfxbench` is the opposite —
   it draws continuously, so stillness *is* its completion signal.
