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

> **SOLVED — read the FOURTH session first.** The defect is `gfx_points`
> running its one-bit inline loop on a PLANAR display with `ES = 0`
> (SPEC.md §5.6.9.5.2), introduced by `782f6b6c` and fixed in 15 bytes of
> `.text`. Every *"what to do next"* in the first three sessions below is
> spent, and two of them point somewhere the fourth session disproves — read
> them for the eliminations, which stand and were all correctly measured, and
> not for the leads. The single sentence that explains why three sessions of
> instruments saw nothing: **they all watched `LOW_SEG`, and the write is to
> `KERNEL_SEG` and to segment 0.**

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

15. **`vid_span_one` is not where SP breaks.** Breakpoints on its entry and
    its exit, comparing SP across the call: **83 entries, 83 exits, SP legal
    at every one** over ten round trips - including the rounds after the
    window had stuck on the Hercules. That also measures something useful
    about the other symptom: once the drag back is a no-op, `vid_span_one`
    stops being called AT ALL (its count freezes at 5, and at 83), because
    nothing re-lays-out. So the no-op drag is upstream of any drawing.

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

---

# Third session, 2026-09-21: the stack is a CONSEQUENCE, and the live lead is `[gfx_dnest]`

Same tree, same box, same machine (`os8088_xt_vga_herc`, build 950). The
second session left "the stack pointer leaves its stack" as the lead to pick
up. **It is picked up and it is spent**: SP going wild is downstream of the
wreck, not upstream of it, and this section is the measurement that closes it
plus the one thing that is now known to break while the machine is still
alive.

## The machine, so nothing below has to be re-derived

Three tasks exist for this scenario and no more, read off `sch_stkbase` /
`sch_stksize` / `sch_tasks` on the running guest:

| slot | task | slice | where |
|---|---|---|---|
| 0 | the UI task | 512 | `STK0` 18DE..1ADD, top SP **1ADC** |
| 1 | the idle task | 128 | 0AD6..0B55 |
| 10 | MISSILE's worker | 256 | 10D6..11D5 |

Slots 2-9 and 11-13 are unspawned, and an unspawned slot has **neither a
`SCH_MAGIC` canary nor a 0xCC fill** - both read 0000. A probe that checks
every slot rather than every *live* slot reports twelve smashed canaries on a
perfectly healthy desktop, which cost one run here.

The two private stacks sit immediately below the slices and are exactly
adjacent: `mou_pstack` 09D6..0A55 (top SP **0A56**), `sch_chstack`
0A56..0AD5 (top SP **0AD6**) - and `sch_chstack + SCH_CHSTK` **is**
`sch_stacks`, so the chain stack's top word is slot 1's canary.

## Five more things it is NOT

16. **Not a stack overflow, in any slice.** Every live canary reads 5A57 in
    every dump taken here, including the dumps of wrecked machines, and the
    0xCC fill at the deep end of each slice is untouched. A downward runaway
    cannot leave a canary it passed through intact, so there is no runaway.
17. **Not a pop off the TOP of a stack either.** `LOW_PARA` rounds
    `.lowbss + STK0` up to a 512-byte rung, which leaves LOW_SEG:1ADE..1BFF -
    **290 bytes belonging to nothing at all**, directly above the UI task's
    stack top. Six `mem` wires there (and a `mem` breakpoint fires on a READ,
    which is what an over-pop does): **40 round trips across four lanes, not
    one hit**, and one of those lanes died in the usual way with the wires
    quiet.
18. **`gfx_blit1_x`'s `mov sp, bp` / `add sp, 18` is innocent** - the only
    instruction in the kernel that can move SP by an arbitrary amount in one
    step (every other SP write is `mov sp, imm16` or a restore from a slot
    whose value is checked below). An exec wire on it took **0 hits in 16
    round trips**: `gfx_blit1` is not called in this scenario at all.
19. **The cursor's display bracket is safe.** `CUR_DBEGIN` and
    `cur_move_multi` both bank the outgoing display in ONE static byte,
    `[cur_dprev]`, and `cur_get` is reached from the deferred hide *and* from
    the mouse ISR - which reads like a re-entrancy hole, and is not: all three
    task-side entries (`kbm_paint`, `cur_lazyend`, `mou_apply`) are inside
    their own `pushf`/`cli`, so the ISR cannot nest inside one.
20. **The chain stack and the mouse ISR's stack are clean, and their contents
    decode exactly as designed.** `[sch_chsave]` and `[mou_psave]` hold legal
    values in every live dump. A chain stack caught mid-tick reads, top down:
    `[0AD4]` = **F002**, the `pushf`'d FLAGS with IF=0; `[0AD0..0AD3]` =
    `0060:4920`, which resolves to **`sch_isr.full+30`** - the return address
    of `call far [sch_old08]`; and below that an interrupt frame taken at
    **F000:FEC7 with FLAGS=F207 (IF=1)**, which is the ROM's own `sti` window
    with something nesting harmlessly inside it, exactly as SPEC.md 8.5.1
    says.

## So where the wild SP comes from

It is the wreck, seen later. The wire at STK0's deep end (`STK0+16`, 18EE)
did eventually fire - and when it did the CPU was at **0000:0726**, the
kernel's own `.bss` already read 0xFF (`vid_cur` 255, `vid_ndisp` 255,
`gfx_lock_flag` 255) and 420 bytes of STK0 held one repeated word. The
SP values this investigation has been chasing - **0210, 01F4, FF20, 18EE,
25A8, 005C** - are all of that kind: a CPU already executing garbage,
pushing and popping wherever it lands. `SP = FF20` is not a 7,100-byte fall,
it is a wild `push`/`pop` pair in wild code.

One reading is worth keeping because it looked like a smoking gun and is not:
`vid_span_one.out+3` (`pop ax`) caught with **SI = 5A57 and BX = CCCC** - the
canary and the fill - on a machine whose every other word was sane. That is
`.out` popping off the top of `sch_chstack` into slot 1's untouched fill.
It is real, it is downstream, and it is what a wild CPU landing on a pop run
looks like.

## THE LIVE LEAD: `[gfx_dnest]` = 106

Caught by a LOW_SEG snapshot differ - no breakpoints, so nothing suppressed -
with the machine **alive**:

```
CPU 0060:1F5D vga_solid_rect.lcol   IF=1  SS:SP=1940:1AAE  DS=0060 ES=A000
gfx_dnest = 106          <-- legally 0 or 1 on this machine
vid_cur 0   vid_mono 0   vid_planes 4   vid_ndisp 2
gfx_lock_flag 1  gfx_lock_own 0  sch_cur 0
| ui_task.drag -> ui_drag -> task_yield
| ui_drag.no_evt+53 -> ui_drag_xor
| gfx_xor_rect_d+11 -> gfx_xor_strips
| gfx_xor_strips+37 -> gfx_xor_fill_raw
```

IF is 1, SP is inside STK0, the lock is held by the task that is drawing, and
every other word reads correctly. This is not the wrecked machine scribbling
on that byte - **it is 105 unmatched `gfx_disp_enter`s**, and the CPU is
inside the DRAG OUTLINE when it is seen.

**It does not contradict the second session's "984 increments against 984
decrements, exactly balanced".** That measurement armed eight breakpoints, and
eight breakpoints are enough to change the pacing: the same run's lanes did
not die either. A balanced count on a machine that did not fail is a
measurement of the healthy path.

What `[gfx_dnest]` at 106 does to the machine is not a memory smash by itself,
and that is why it has to be chased rather than assumed: above 1,
`gfx_disp_enter` **stops translating and stops selecting a display**
(`inc` / `cmp 1` / `jne .out`), `gfx_points` takes `.slow` for ever,
`font_ch_cut` refuses every seam cut, and every primitive draws on whatever
display happens to be current with the OUTER hook's translation assumed.
vga12.inc's own banner at `vga_xor_rect_vram` describes what that costs when
it goes the other way: a rect dispatched on the wrong `[vid_mono]` wrote
through `ES:DI = 0960:8FC1`, which is `COLD_SEG:45C1` - **the file manager's
own code** - and the machine rebooted or froze.

Also seen once, on a dead machine: `sch_tasks[0].T_SP` = **1119**, inside slot
10's slice. The UI task and MISSILE's worker parked on one stack.

## A methodological finding, because it invalidates comparisons

**The reproduction rate depends on the HOST probe's pacing**, and this is not
contention. Mouse packets are injected over the debug socket, so the host
loop's period decides how much guest time passes between them - and the
defect lives in that window. Measured here, same tree, same scenario, four
lanes each:

| probe, per sample | rounds | deaths |
|---|---|---|
| 110 KB `.text` read | 12 | 2 of 4 |
| 7 KB LOW_SEG read | 14 | 3 of 4 |
| `regs` + 290-byte read | 14 | 2 of 4 |
| one 1-byte read | 14 | **0 of 4** |

A probe that samples cheaply does not reproduce the bug at all. **Any future
measurement here has to state its per-sample cost**, and two runs with
different probes are not comparable.

## ...AND `[gfx_dnest]` IS SETTLED, NEGATIVELY

Written the same day, an hour after the section above, because the section
above is wrong and leaving it standing would cost the next person a session.

`[gfx_dnest]` was measured again with an instrument that costs the HOST
nothing and therefore does not suppress the defect: a **DNESTLOG=1 knob** that
rings the SITE of every touch into 128 bytes of `.text`. It is ~30 lines and
takes ten minutes to put back:

* a `DNMARK n` macro next to `gfx_dnest`'s own declaration in `kernel/vga12.inc`
  - `pushf` / `push bx` / `mov bx, [cs:gfx_dn_rp]` /
  `mov byte [cs:bx+gfx_dn_ring], n` / `inc bx` / `and bx, DN_RING-1` /
  `mov [cs:gfx_dn_rp], bx` / `pop bx` / `popf`, with `DN_RING equ 128`, and an
  empty `%macro DNMARK 1` on the other arm;
* one `DNMARK` above each of the seven `inc`/`dec byte [gfx_dnest]`, the id's
  high bit set for an increment. **Not the one in `gfx_blit1_x`**: that routine
  is in `COLD_SEG`, so `cs:` there addresses the wrong segment - and it is
  never called in this scenario anyway;
* `GFXDLEAVEI` becomes `%macro GFXDLEAVEI 0-1 <default>` so its two expansions
  (`font_char.done`, `font_run_x.out`) ring different ids;
* `DNESTLOG` into the Makefile's `VIDDEF`, `$(VIDSTAMP)` and `$(KNOBS)`;
* build with `python3 tools/os88build.py build DNESTLOG=1`, and drive it with
  `OS88_BUILD=<abs tree>` and `OS88_DEFINES="KERN_BIG DNESTLOG KERN_KNOB"`.
  (`ls -d build/trees/dnestlog-*` matches the `.lock` directory too - name the
  real one or every symbol lookup dies on a missing `associco.inc`.)

**The ring says the pairing is EXACT.** Read at the first value outside 0..1,
the last 48 touches alternate without a single exception:

```
gfx_disp_enter INC / font_char.done dec (GFXDLEAVEI)
gfx_disp_enter INC / font_char.done dec
gfx_disp_enter INC / gfx_dleave     dec
gfx_disp_enter_n INC / gfx_dleave   dec        ... 24 pairs, no gap
```

And the count at the moment of death, four lanes: **0, 1, 1, and 255 - and the
255 lane had `SS = 0000` and `CS = 3B07`**, i.e. was already executing garbage.
So `[gfx_dnest]` is sane in three deaths out of four, and the 106 / 250 / 255
readings are the wreck scribbling on that byte after all. The second session's
"984 against 984" conclusion was right for the wrong reason, and the reading
that overturned it here - "IF=1, SP legal, every other word sane, therefore
alive" - **is not a test of aliveness**: a wild CPU executes kernel code with
IF set and a legal SP for a good while before it hits anything that shows.

## Where this went

Not `[gfx_dnest]`, and not the stack. The answer is the fourth session below,
and the lead this section proposed — pair the `LOW_SEG` differ's pacing with a
`[gfx_dnest]` alarm — was never run, because the lead itself was wrong twice
over. Kept as written so the negative result stays legible; do not work it.

# Fourth session, 2026-09-21: FOUND — `gfx_points` asks the ADAPTER before it asks the HOOK

**`gfx_points` runs a ONE-BIT inline loop on a PLANAR display, with
`ES = 0`.** The store is `mov [es:di], ah` and `di` is a row base, so on this
machine it walks the IVT, the BIOS data area and the kernel's own `.text`
from `0x0600`. That is the wild write every previous session was downstream
of: the stack going wild, the `0xD2` fill, the masked IRQ0, the `[gfx_dnest]`
of 106 are all the wreck, and none of them is the defect.

It is SPEC.md §39.14.6's documented failure class one routine along. That
banner is about `sw_col` and ends *"the machine rebooted or froze."*

## The mechanism, in the order the instructions run

```nasm
    cmp byte [vid_mono], 0      ; (1) the ADAPTER, asked of whatever display
    je .slow                    ;     the LAST primitive left current
    cmp byte [vid_planes], 1
    jne .slow
    ...
    mov ax, [es:si]             ; (2) the FIRST POINT's display
    mov bx, [es:si+2]
    call vid_disp_of
    mov dl, al
.hook:
    call gfx_disp_enter_n       ; (3) ...which is ENTERED here
    jmp short .pass             ; (4) and the one-bit loop runs on it
```

Step 1 and step 3 are about **different displays**. §39.14.3 restores none on
purpose — the last display drawn on stays current — so step 1 describes the
Hercules whenever the Hercules was drawn on last, and step 3 enters the VGA
whenever the array's first point is on the VGA. `.pass` then loads
`ES` from *that* display's `[vid_rseg]`, which is **0** on a planar primary,
and runs `and ah, [es:di] / or ah, al / mov [es:di], ah`.

`.done`'s second pass is worse, because it has no test at all:

```nasm
    or byte [gfx_pt_f], PT_2ND
    mov dl, [vid_cur]
    xor dl, 1                   ; the OTHER card, whatever it is
    jmp .hook
```

so **every array that straddles the seam** enters the other card
unconditionally. That is why the reproduction is a window DRAG onto the second
display and nothing else: it is the one gesture that splits a point array.

## The bisect

`782f6b6c` — *"Merge cyclone-extdesk-perf: `gfx_points` on the extended
desktop"*. It removed the `cmp byte [vid_ndisp], 1 / ja .slow` that had sent
every two-display call down the per-point path, added the hook and the
`xor dl, 1` second pass, and **reordered the two adapter tests above the
`[vid_ndisp]` test** on the way.

Four lanes, eight round trips each, GOOD requiring 0 of 8 dead:

| position | commit | verdict |
|---|---|---|
| 0 | `c045f472` | GOOD 0/8 |
| — | `924eedb3` | GOOD |
| 10 | `5d7e8d43` | GOOD 0/8 |
| 15 | `bf118e46` | GOOD 0/8 |
| 17 | `7483e388` | GOOD 0/8 |
| **18** | **`782f6b6c`** | **BROKEN 1/4** |
| 20 | `47efde8d` | BROKEN 1/4 |
| 40 | `6c4bff0c` | BROKEN 3/4 |

Last good and first broken are adjacent, and the first broken is the merge's
own content — `7483e388` is its first parent. The user's warning about a
squash did not bite: the branch merged rather than squashed, so the commit
that carries the change is in the history.

Three rules made the search answer at all, and `tools/os88bisect.py` states
all three: **never take a side from one run** (this defect is ~75% per round,
so N=1 names a random commit), **parse legs** and **check ancestry first**.
Point 18's first batch read 0/4 and was **contaminated** — the on-machine
proof was running beside it, six emulators on four cores — and the clean
re-run read 1/4. A bisect point that reads GOOD under load is the failure mode
to watch for here.

## The proof, on the machine

An exec breakpoint at `gfx_points.pass`, on `os8088_xt_vga_herc` with the
window dragged across the seam:

```
vid_cur=0  vid_mono=0  vid_planes=4  vid_rseg=0000   =>  ES = 0000
```

and at `.hook` with `[gfx_pt_f] = 0x03` (`PT_OOB | PT_2ND`) — the straddling
second pass, entering the other card.

## Why three sessions of instruments did not find it

**Every probe watched `LOW_SEG`.** The task table, the slice canaries, the
dead-space tripwire in the 290-byte rung gap, the `.lowbss` differ — all of
them. The write is to `KERNEL_SEG` and to segment 0, so there was nothing for
any of them to see, and the stack only goes wild *after* the kernel's own code
has been drawn on. Three sessions of eliminations were all true and all
downstream.

## Why the suite stayed green

`tests/ptsext.py` drives all three of §5.6.9.4's extended-desktop paths and
self-compares band against band — and it boots `os8088_5150_both_gla_mono`,
**Hercules primary, CGA second**. Both displays are 1bpp there, so the two
tests at the door are true of either card, the hook can never enter one the
loop cannot write, and the defect cannot be expressed at all. The row was
written against the pair the original field report came off; the defect needs
a MIXED pair.

## The fix, and what it cost

The two tests move BELOW the hook, and a display the loop cannot serve gives
the hook back and takes `.slow` — which is what a two-display call did before
§5.6.9.4 existed. SPEC.md §5.6.9.5.2 is the contract.

**15 bytes of `.text`**, measured as the difference between two assemblies of
the same tree: `.text` 50,144 → 50,159, `.bss` +0, `.cold` +0, `.lowbss` +0,
`KERN_SIZE` unmoved, no rung crossed. `kern_small` is **+0** — the whole block
is inside `%ifdef GFX_VGA`, which is `KERN_BIG`-only.

Verification, the same instrument the bisect used: **four lanes × eight round
trips, twice — 64 round trips, 64 crossings, zero deaths**, with the window
returning cleanly to `(7, 20, 562, 435)` each time. HEAD before the fix was
3/4 dead in round 0.

## The gate

`tests/ptsmix.py`, on `os8088_xt_vga_herc`. It asserts the INVARIANT and not
the crash: an exec breakpoint at `gfx_points.pass` — the instruction before
`mov es, bx` — reads the display the loop is about to write to, and every
sampled pass must have `[vid_mono]` set, `[vid_planes]` 1 and `[vid_rseg]`
non-zero. That fires before the damage, so the row names the defect rather
than reporting the reboot it causes twenty frames later.

Two things about it are worth keeping, because the first shape of the row was
**green against the broken kernel**:

* **the WINDOW straddling the seam is not the point ARRAY straddling it.**
  PtsTest's bands are 120px wide inside a 176px window, so a frame placed
  across the seam leaves every point on the primary, `PT_OOB` is never set,
  the second pass never runs and the row sees a healthy kernel. The straddle
  target is computed from the band, not the frame;
* **`drag(…, tgt)` was in POINTER coordinates** where the row meant window
  ones. The grab is the title bar's midpoint, so the window landed half its
  own width short — which is exactly far enough to do the same thing.
