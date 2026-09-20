# Moving a window onto the second display takes the machine down

**A measurement, not a description.** Taken 2026-09-20 on a four-core cloud
container at commit `40e60ca1` (build 938), under MartyPC on the
`os8088_xt_vga_herc` machine — a VGA primary at 640x480 and a Hercules second
card, extended to the right, so the virtual desktop is 1360x480 with the seam
at x=640. Quote it with that box; every figure below that is a count of guest
cycles or of guest ticks is exact at any width, and the host wall clock is
not. It is not maintained against a later tree.

It is the diagnosis behind `tests/dispmodex.py` being red, and behind the two
things that row now says instead of what it used to say.

## The finding, in one line

**Dragging MISSILE's window onto the Hercules half of an extended desktop
kills the guest about three runs in four.** The pointer then does not move,
which is how the row reported it for months: `could not reach (876,29):
stuck at (840,29)`, a sentence about a coordinate.

## That it is the MACHINE and not the harness

The watchdog is the kernel's own `[ticks]` at `S("ticks")`, which only IRQ0
advances, sampled against MartyPC's cycle counter — so the unit is GUEST
seconds and a loaded box cannot shorten it. Three presentations, all from the
same scenario:

| | what the guest did |
|---|---|
| spin | `[ticks]` 1322 → 1322 while the emulator burned 7.0M cycles per 0.5 host s; CPU pinned at `9C00:3FAE` (MISSILE's own region) across 24 samples with **IF=0** |
| smash | the whole of the kernel's state read `0xD2`: `[ticks]` = 0xD2D2, `sch_chbusy` = `sch_fast` = `sch_lock` = `sch_coop` = `sch_cur` = 210, every task record garbage, every slice canary gone, CPU executing FAT directory entries at `FAT_SEG:CE37` |
| reboot | `[ticks]` = 0, no package windows, 8259 IMR bit 0 set (IRQ0 **masked**), SS not `LOW_SEG`, `int 01h` taken from a heap segment into the ROM's spurious handler at `F000:FF23` |

And once, cleanly, with the fix to the row in place:
`mc_caps=0x9E9E mono=158` — MISSILE's own bss filled with `0x9E` — followed
by `[ticks]` stuck for a whole guest second.

A fourth run died so completely that `os88marty.settle` reported the guest
clock not moving for 62.1 s "at `'?'` at `0000:0000`".

## Rate

`tests/dispmodex.py` at this commit, four concurrent lanes: **1 pass, 3
deaths.** A loop that only drags the window across the seam and back, with
the geometry settled between drags, survived **12 of 12** round trips in each
of four lanes; the same loop with no wait between them left the window where
it was on **13 of 14** round trips and took a lane down.

## Four things it is NOT, each measured

These are written down because each one is the obvious suspect and each one
costs a session.

1. **Not the mouse ISR's private stack.** `MOU_PSTK` is 128 and SPEC.md 9.10
   sizes it at "2.4x the 54 the chain measures on the deepest adapter" — a
   figure taken on one-display machines. Filled with a sentinel and walked:
   **54** on a single VGA, **60** extended with the pointer on the VGA, **48**
   on the Hercules, **60** crossing the seam, **60** during the drag itself,
   **48** with the pointer over the straddling game window. The extended
   desktop costs it **six bytes** and the margin is still 2.1x.
2. **Not MISSILE's worker slice.** Class 256 (`OS88_STACK_256`). High water
   off the kernel's own 0xCC fill: **156 of 256** on one display, **188 of
   256** extended, and flat at 188 over 16 further seconds of straddling. The
   extended desktop costs it **32 bytes**; the margin falls 1.64x → 1.36x,
   which is thinner than the survey that set the class (STACK-SLOTS-PLAN 12)
   ever measured but is not an overflow.
3. **Not task 0's stack.** `STK0_SIZE` = 512, and `tests/stk0water.py`'s probe
   driven over this scenario reads **266 of 512** on one display against
   **268 of 512** extended — **two bytes** — with the canary at `STK0_BOT`
   intact in every dump.
4. **Not the seam clamp.** SPEC.md 39.15.4's `mou_clamp` was walked at nine
   heights with 20px steps, bare and with MISSILE up, and crosses
   **deterministically** at y = 29, 100, 200, 300, 340, 347 and 348 — always
   at exactly 620 → 640. At y = 400 and y = 470 it correctly refuses: display
   1's record is `vx=640 vy=20 cw=720 ch=348`, so those rows are past its
   bottom edge and the point is in the dead zone, which is the behaviour the
   routine's own header describes. `mou_nx` reads 659 and `mouse_x` stays 639,
   which is the clamp working.

## What the wreckage points at

Every post-mortem that landed inside the kernel landed in the drawing layer,
reached from the UI task's drag:

```
ui_task.drag -> ui_drag -> ... -> fpg_clear -> gfx_fill -> gfx_clip_run
gfx_points.ink -> gfx_pt_row -> gfx_rowbase_calc
gfx_points.paper -> gfx_ls_box -> gfx_clip_query
```

The sharpest single frame is `gfx_clip_run.next` with **`DS = 0x01C7`** —
neither `KERNEL_SEG` nor anything else this machine has — and **`DI = 0xA1B2`**,
which is `[wm_clip_n]` read through that wrong `DS`: 41,394 fragments to walk
instead of a handful. `ES` was `0x0F58`, also nothing. `CS`, `SS` and `IP`
were all correct. Two segment registers restored as garbage with the third
correct is a `pop` from a stack that had something else on it.

MartyPC's recovered call history in that run carries two nested
`INT 0Ch` (IRQ4, the serial mouse) frames taken from segments that do not
exist, and one frame where `DS` holds the same value as the return address
of the frame below it.

## What reproduces it and what does not

| | deaths |
|---|---|
| `tests/dispmodex.py` as it stands | 3 of 4 |
| drag across the seam, then nothing for 30 guest s | 0 of 4 |
| drag across the seam, then 400 mouse packets | 0 of 4 |
| drag back and forth 6 times with the geometry settled | 0 of 4 |
| drag back and forth 14 times with **no** wait between | 2 of 4 |
| pointer sweeps and menus for 90 s, extended, MISSILE up, **no drag** | 0 of 4 |
| the same on ONE display | 0 of 4 |

So it needs the extended desktop, it needs the window to move onto the second
display, and it needs a second UI event soon after — which is exactly what
`dispmodex` does and what a person does.

**The window RESIZES when it lands there**, 562x435 → 634x303, because
`mc_onresize` re-reads the adapter (`mc_mono` 0 → 1) and `mc_layout`
recomputes every row and column from the new content size. `mc_onresize` also
sets `[mc_full]`, so the next frame owes a WHOLE repaint of the playfield —
on the 1bpp adapter, with the window straddling the seam, which is the
heaviest drawing path in the tree. Nothing in that path claims heap, so a
compaction is not in it: `OSAPI_MEM_*` is never called and MISSILE's region
segment reads `9C00` before and after in every run.

## What is not answered

Which store writes outside its own object. A memory breakpoint just past
MISSILE's bss never fired in four runs, and a breakpoint on an unspawned
task slice fired only on an ordinary `push` from a slice that turned out to
be live. A whole-of-low-memory snapshot diff is the instrument that would
settle it and it is too slow over the debug socket to take twice inside one
run — 110 KB a read, and four lanes did not finish it in 900 s. The next
person should take it a page at a time over a fixed list of addresses, or
put an `io`/`mem` breakpoint on `0xB0000` and ask the opposite question:
which stores reach the Hercules framebuffer, and from where.

## What the row says now

`tests/dispmodex.py` used to report the coordinate its pointer could not
reach. It now:

* quiesces on MISSILE's **geometry as well as its caps** before the next
  drag, because `mc_caps` is written the moment the worker next asks
  `fsx_caps` and the re-layout is still running behind it;
* asks, after each move, whether `[ticks]` has advanced inside one guest
  second, and says **"THE GUEST HAS STOPPED"** with the stuck value if not;
* asks whether MISSILE's window is still in `wm_wins`, and says **"MISSILE'S
  WINDOW IS GONE"** with the live window list if not — a dead window's record
  still reads its last geometry, so the next drag grabs where there is no
  longer a window;
* and reports a drag back that left the window's centre right of the seam as
  a no-op in those words.

None of that fixes the defect. It makes the row name it.
