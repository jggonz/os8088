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

---

# Second session, 2026-09-21: six more eliminations and where the earliest signal is

Same tree, same box, same machine. Everything below is measured; the defect is
still not root-caused and this section exists so the next person does not
re-derive any of it.

## The instrument, first, because two of its properties are not obvious

* **A MartyPC `mem` breakpoint does NOT fire on an instruction fetch.**
  Measured: a wire on `gfx_fill+4`, executed constantly on a live desktop,
  never fires; a wire on `[ticks]` fires at once. So **every byte of kernel
  code is a usable tripwire** - it is touched by nothing unless something
  writes where it should not.
* **A quietness probe has to run with the machine BUSY.** An `int` READS its
  vector, so a live vector is a data access to the IVT - and a probe taken on
  an idle machine called `int 0Ch`'s vector dead because no mouse packet
  arrived inside the window. Four lanes then tripped on the first packet of
  the drag, at `mou_isr`'s first instruction, which is the gate reading
  0x0030 and not a wild write at all. Each probe window now carries a mouse
  packet and spans two timer ticks.
* **It is a Heisenbug and the threshold is measurable.** 42 wires reproduce
  the crash 2 runs in 4; **287 wires suppress it outright** (40 round trips,
  0 deaths, and the window came back every time). Keep a wire set small.
* A full `.text` read is **16 ms for 110 KB**, so snapshot-diffing the whole
  kernel faster than the guest runs a frame is free. (An earlier attempt
  "timed out" for a reason that was in the script, not the socket: a poll
  loop bounded by `m.status()["cycles"]` never terminates on a guest that has
  stopped executing.)

## Six more things it is NOT

7. **Not a wild write to the IVT.** `vid_rseg` is 0 on a VGA primary, which is
   legal, and `viddet.inc` warns that a 1bpp path through a `vid_rseg` of 0
   writes to the IVT - so that was the obvious next suspect. It is not
   happening: `int 08h`, `int 0Ch` and `int 0Bh` read `c5486000`, `913c6000`
   and `a73c6000` in **every** post-mortem, including the machine whose whole
   kernel state read 0xD2 and the one that had rebooted.
8. **Not a wild write to the window table.** 52 tripwires through the unused
   `wm_wins` slots, 12 round trips, four lanes: `wm_wins` is never touched.
   Three of those lanes died anyway.
9. **Not a bad display index.** One exec breakpoint on `vid_ctx_act` (the only
   writer of `[vid_cur]`), four lanes, ~6,000 calls: **every one passed AL of
   0 or 1**. The `vid_cur` = 116 seen earlier is therefore a wild write to
   that byte, not a caller's mistake.
10. **Not a leaked `cli`.** A busy machine holds IF=0 for at most 7-10
    consecutive samples; an alarm at 3x that never fired across 40 round
    trips. When IF *does* stick it is total - 300 of 300 samples - and by then
    the CPU is already executing garbage.
11. **Not an unbalanced display nest.** The image contains exactly **five**
    `dec byte [gfx_dnest]` and **three** `inc byte [gfx_dnest]` (found by
    scanning for `FE 0E`/`FE 06` against the symbol's own offset), and a
    breakpoint on all eight logs both directions. One lane: **984 increments
    against 984 decrements, exactly balanced**, over eight round trips. The
    250 / 253 / 254 readings earlier in this file are the wrecked machine
    scribbling on that byte, not an accounting error. `gfx_blit1_x.nopen`'s
    decrement was caught *at* 0 once - with `CS=0000`, every register zero and
    a callstack of garbage, i.e. the wild CPU wandering onto that address.
12. **Not the seam-cut paths, on inspection.** `font_ch_drop` reaches
    `font_char.done`; `font_ch_spill` rejoins `.edgeok`; `gfx_blit1_x`'s
    `.percol` and `.okquiet` take no hook and run no leave; `gfx_blit4` and
    `gfx_blitp` each guard their leave with a per-call flag
    (`[gfx_blit_hk]`, `[gfx_bp_hk]`) added for exactly this hazard -
    vga12.inc:2695 describes it and what it looked like.

## Where the earliest signal is: THE STACK POINTER LEAVES ITS STACK

This is the lead to pick up. Every legal stack is known
(`STK0` 18DE..1AE0, the slices 0AD6..15DE, `mou_pstack` 09D6..0A5E,
`sch_chstack` 0A56..0ADE, all `SS = LOW_SEG`), a baseline of ~540 samples is
inside one of them 100% of the time, and the alarm is the first sample that is
not. Two catches, and **both had IF still 1** - so this precedes the interrupt
death, the smashed memory and the stuck pointer:

```
wm_hit.next     IF=1 ss:sp=1940:1AC8 ds=0060 es=0060   ok
MISSILE+2AFA    IF=1 ss:sp=1940:11A8 ds=9C00 es=0060   ok
gfx_ls_box+1    IF=1 ss:sp=1940:005C ds=0060 es=0060   BAD   <- 4,428 bytes in one sample
                gfx_dnest = 250, lock free, sch_cur = 10 (MISSILE's worker)
```

```
gfx_ls_box.x2ok+3  IF=1 ss:sp=1940:1A36 ds=0060 es=0000   ok
gfx_ls_box.x1ok+10 IF=1 ss:sp=1940:1A36 ds=0060 es=B000   ok
vid_span_one.out   IF=1 ss:sp=1940:25A8 ds=0060 es=9C00   BAD   <- above STK0 entirely
                   gfx_dnest = 0, lock held by 0, sch_cur = 0 (the UI task)
```

SP runs **down** out of a worker slice in one and **up** out of STK0 in the
other, and both land in `gfx_points`' helpers - `gfx_ls_box` and
`vid_span_one` - on a machine drawing on the second display. A third ring,
caught on the IF alarm, shows SP climbing **+80 bytes per sample** through
`font_char.row` / `font_char.chok` / `vid_ctx_act`, which is a net pop per
glyph cell.

Beside it, one more register fact worth keeping: `gfx_fill_pat_raw.irow` was
caught running with **`ES` = MISSILE's own segment** and `SI` = 8,482 rows
left to fill, and `gfx_points.paper` with **`CX` = 0xFEF7 points** and `BP`
walking MISSILE's *code* rather than `mc_pts`. A fill whose `ES` is a package
region instead of a framebuffer is a wild writer by itself.

## Two more eliminations, from following that lead

13. **The bytes below the stacks are not free, and wiring them is a false
    positive.** `.lowbss` there is font scratch, not slack:

    ```
    09BE  font_zero      8
    09C6  font_seam_a    8      SPEC.md 39.14.11's two half-cells
    09CE  font_seam_b    8
    09D6  mou_pstack   128      the mouse ISR's private stack
    0A56  sch_chstack
    0AD6  sch_stacks
    ```

    All four lanes tripped identically at `font_run_x.rmpx+7` with
    `SS:SP = 1940:1A3C` - **inside STK0, so no stack had moved at all**. The
    wire was on the scratch that loop reads through `ss:bx`, and the quietness
    probe had missed it because it never ran while text was drawn on the mono
    display. Anything wired in `.lowbss` has to be probed with the machine
    doing the thing under test, not merely busy.

14. **The seam scratch does not overrun into the mouse ISR's stack**, which
    the layout above makes the obvious suspect: `font_seam_b` ends at `09D6`
    and `mou_pstack` begins there, with no gap, so any overrun of the seam
    halves lands on the stack whose corruption produces exactly the observed
    "wild CPU with IF=0". The ISR's measured high water is 60 of 128, so its
    bottom ~68 bytes are dead space and can be wired: ten wires through
    `mou_pstack + 0..36`, twelve round trips, four lanes. **Never fired.**
    Two of those lanes died in the usual way while the wires stayed quiet.

    And the count-of-zero hazard that reading `font_ch_cut` suggests is NOT
    real - checked before writing it down. `.byz` does `mov cx, bp` and a
    `loop`, which would run 65,536 times at `bp = 0` and write 64KB starting
    at `mou_pstack`; but `bp = [vid_ch] - dx` and `.live` has already refused
    `dx >= [vid_ch]`, so `bp` is 1..8 and `bx` is 1..8 for the same reason.
    The comment there ("Both are 1..8") is right, and what proves it is the
    ANCHOR test at `.live`, not the `jae .part` the loop comments cite.

## What to do next

Not the stack floors - 13 and 14 above spent that idea. What is left of the
SP finding is that SP is seen outside every legal stack **while IF is still
1**, twice, in `gfx_points`' helpers, and no wire catches the write that
does it. The next instrument is therefore the one that does not need to guess
an address: **an exec breakpoint on `gfx_ls_box`** (or `vid_span_one`) that
reads SP at entry and stops on the first value outside the legal set. It is
one breakpoint, so it is under the suppression threshold, and it names the
call that arrives already broken rather than the write that broke it - which
is the step this investigation has not been able to take any other way.

Worth pairing with it: the two register facts that no hypothesis here has
accounted for - `gfx_fill_pat_raw.irow` running with **ES = a package's own
segment** instead of a framebuffer, and `gfx_points.paper` walking with
**CX = 0xFEF7** and BP inside MISSILE's code rather than `mc_pts`. Either one
is a wild writer on its own, and neither is explained by anything eliminated
above.
