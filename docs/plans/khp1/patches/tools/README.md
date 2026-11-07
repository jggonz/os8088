# Kernel hot-paths pass 1: the agents' scratch tools, parked

These are the harnesses and instruments the pass's agents wrote to prove and
price their changes. They are **specific to this pass's items**, so they are
parked here beside the patches rather than in `tools/`: the place to look
before turning a `KHP_*` knob on, or before re-doing an item. They are kept as
their agents left them, with only paths into deleted worktrees and scratch
directories replaced (each now finds the checkout it sits in, or `$OS88_ROOT`,
or takes the trees as arguments). **None has been re-run since the pass**:
expect to adapt one before trusting it.

The generic results of the pass are not here: `tools/os88hot.py` (the
instrument), `tools/os88knob.py` (the knob merge), `tests/unit/t_khpknobs.py`,
`tests/navcalls.py` and `tests/fdswap.py` (rows).

## equivalence/ - a rewrite against the base kernel's own bytes, under unicorn

Run one with a BASE tree (or build dir) and a TIP tree built with the knob on.
Each compares registers, memory and, for the drawing ones, every framebuffer
byte, over random and edge cases. Each was shown to fail on a deliberate break
when its agent wrote it.

| script | re-checks | how its agent ran it |
|---|---|---|
| `disk_lzeq.py BASE_ROOT TIP_ROOT [seed] [nfuzz]` | `KHP_LZ` (disk-1, disk-2): CF, DI, the poisoned output arena, DS/ES, over every packed package in place and apart, truncations, wrong lengths, mutations, outputs past 64KB | 914 cases, identical |
| `mono_uni.py BASE_ROOT TIP_ROOT [N] [--only fill,xfer,...]` | `KHP_MONORECT`, `KHP_MONOSCROLL`, `KHP_MONOPAT`, and mono-2 (shipped): fills, grey, XOR, pattern, save, restore, scroll on CGA and Hercules geometry | 11,996 cases, identical |
| `pointer_vgaeq.py BASE_BUILD TIP_BUILD [--n N]` | `KHP_VGASAVE`'s planar cursor save/restore, under a four-plane VGA model | 1,500 cells, identical |
| `pointer_moveq.py BASE_BUILD TIP_BUILD [--seqs N]` | pointer-1/4/5 (shipped): `cur_move_mono`, `cur_put_mono`, `cur_get_mono` | 36,000 moves (big), 27,000 (small) |
| `sched_uc_switch.py BASE_ROOT TIP_ROOT [N] [defines]` | sched-1/sched-4 (shipped): `sch_switch`'s pick over random task tables | 5,000 + 3,000 cases |
| `gfxcall_eq_rect.py BASE_BUILD TIP_BUILD [N]` | gfxcall-1 (shipped): `gfx_rect_setup` over random rects, five geometries | 20,000 rects a geometry |
| `size17_multi_stamp.py` | disk-4's size trim (the `.multi` stamp), self-contained | 3,000 cases |

## instruments/ - MartyPC, guest cycles

| script | what it measures |
|---|---|
| `wm_optime.py ROOT [--machine M] [--json out] [--ops a,b]` | one window or menu operation priced EXACTLY, entry to return of the routine that does it, plus an in-run check that the incremental screen equals a forced full repaint (the wm agent's; `KHP_RELIT`, `KHP_CAPTION`, `KHP_DMGSTALE`, `KHP_RAISEOL`). Shots and JSON go to the current directory |
| `text_fontpark.py --tree . --image ...` | cycle-exact per-call cost of `font.inc`'s routines over 39 cases a display, and the pixels they leave (`KHP_STRRUN`) |
| `htrace.py`, `htrace_run.py`, `htrace_an.py` | a per-instruction single-step trace of a kernel call, attributed by routine: `htrace_run.py` drives a scenario, `htrace_an.py` reads the pickles (gfxcall's per-call cycle figures) |
| `pointer_ptrbench.py <tree> <machine> [--packets N]` | per-byte ISR and per-packet cursor-move cycles |
| `disk_i13trace.py IMG [APPS\|-] [MACHINE] [until]` | every `int 13h` of a boot, from reset to `kmain` or further, with registers; run from the checkout's root |
| `disk_decprof.py SYS APPS MACHINE FOLDER PKG [start] [stop]` | a decode's profile in one launch, by stepping in fixed cycle quanta; run from the checkout's root |

## sizefind/ - the size agent's finders, over a NASM listing

`insns.py` parses a listing (`-l`) into instructions; the rest import it from
the directory named as their second argument. They found this pass's
seventeen size items (docs/plans/khp1/size.md 2.1 says how the dead banks were
proved safe); they are what a next size pass would start from.

| script | finds |
|---|---|
| `dead.py LISTING DIR`, `dead2.py`, `dead3.py` | register saves whose only caller never reads the register: a routine's prologue banks it and every caller loads or discards it before use |
| `dupexit.py LISTING DIR` | identical pop/ret epilogues that could share one exit |
| `pads.py LISTING MAP DIR` | the parity pads in the tree and what each still protects |
