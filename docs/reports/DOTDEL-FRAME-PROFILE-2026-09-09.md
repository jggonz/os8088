# Where a Dot Delirium frame goes

**A measurement, not a description.** Taken 2026-09-09 at `ed22c34` on a
four-core cloud container - nasm 2.16.01, MartyPC at the pinned commit, three
guests at a time. Quote it with that box: **guest cycle counts are exact at
any host load**, because they are counted rather than timed, and nothing in
this file is a host wall clock.

It exists because the corner fix (SPEC.md 93.5.13.3) cost frames on VGA and
the question became which frames were there to buy back. It is true of the
tree it was taken on and of no other; a later measurement is a new file.

## How

Every phase boundary is an execution breakpoint and the cycles between two
consecutive stops are the span of the phase the earlier one opened. **The
guest is halted at a breakpoint, and the PIT with it**, so the pump's host
latency does not enter the numbers and the game evolves exactly as it would
free-running - only slower in host seconds. `scratchpad/ddprof.py` is the
instrument; passes are the frame's top level, the inside of an actor's band,
the OS-side lock bracket, and `dd_step`.

**Two spellings of the load were wrong before this one, and both flattered
the machine by about forty per cent.** Smiles was not being steered, so he
stopped at the first wall; a stationary actor takes `dd_actor_emit`'s
`uok == 2` arm and draws nothing, so the sample was 2.4 emitting actors of 5.
A ghost then caught the stationary Smiles, and `DDS_DIE` and `DDS_READY`
frames - in which `dd_step` never calls `dd_act_move` at all - went into the
same average as playing ones. The figures below steer him, pin his lives, and
keep DDS_PLAY spans only. The first, unsteered run read **53.8% of the tick
where the same build reads 90.3%**.

## The frame, top level

Milliseconds per frame, against a 54.93 ms tick (18.2065 Hz).

| phase | VGA | Hercules | CGA |
|---|---|---|---|
| the five actor bands | **29.93** | **22.33** | **18.84** |
| `dd_step` (game logic) | 8.70 | 6.86 | 6.81 |
| `dd_overlay` + `gfx_unlock` | 3.43 | 1.93 | 2.05 |
| `gfx_lock` + `wm_clip_set` | 3.26 | 1.86 | 1.91 |
| the corner repair queue | 2.63 | 0.03 | 0.03 |
| pellet blink | 0.98 | 0.95 | 0.70 |
| HUD (0.03-0.07 frames in 1) | 0.31 | 0.09 | 0.09 |
| focus check | 0.23 | 0.23 | 0.23 |
| `dd_draw` prologue | 0.10 | 0.10 | 0.10 |
| fruit | 0.02 | 0.04 | 0.04 |
| **total** | **49.59** | **34.43** | **30.80** |
| **share of the tick** | **90.3%** | **62.7%** | **56.1%** |

The two bracket rows are the spans as pass 1 sees them, each holding one
cheap package call beside the kernel one; the section below splits them.

**VGA is the only adapter in trouble, which is what the field said.** The two
1bpp adapters have a third of the tick spare; the VGA has 5.3 ms, and the
scheduler, the mouse ISR and `ui_task` come out of that.

**It is already dropping frames.** `dd_step` runs **1.21 times per rendered
frame** on VGA and 0.99 on both 1bpp adapters - so about a fifth of VGA
frames arrive a tick late and cover two ticks of movement when they do.

## Inside the bands

Per frame, VGA, five actors. `after` is `dd_rep_run` + HUD + overlay +
unlock, and is the same work the table above itemises.

| step | VGA ms | share of band work |
|---|---|---|
| `gfx_blit1` | 12.69 | 42% |
| compose self (`dd_band_one`) | 4.49 | 15% |
| compose dots (`dd_band_items`) | 3.71 | 12% |
| the union, and banking it (`dd_actor_prep`) | 2.63 | 9% |
| compose the other four | 2.18 | 7% |
| ground / wall picture | 2.15 | 7% |
| `dd_split_ck` + `dd_band_build`'s arithmetic | 1.45 | 5% |
| vacated-tile bookkeeping | 1.30 | 4% |
| `dd_actor_emit`'s header | 0.61 | 2% |
| **the two-band split itself** | **0.10** | **0.3%** |

The split fired on **17 of 415 emits (4.1%)** and cost 0.10 ms a frame. The
repair queue it feeds is the expensive half at 2.63 ms, and both are VGA-only:
`dd_split_ck` returns at its first compare when `dd_bpp <= 1`.

## What a band actually is

1,155 bands, VGA, tile 16x13:

| shape | tiles | share |
|---|---|---|
| 32x13 | 2 x 1 | 45.1% |
| 16x26 | 1 x 2 | 33.1% |
| 48x13 | 3 x 1 | 13.4% |
| 16x39 | 1 x 3 | 7.4% |
| 16x13 | 1 x 1 | 0.5% |
| 32x26 | 2 x 2 | 0.4% |

**The band is the pixel union of where the actor was drawn and where it is,
taken out to the tile grid on BOTH axes - and only x has to be.** `gfx_blit1`
wants byte columns, which a multiple of 8 gives it; y is rounded for no reason
the blit has, and `dd_band_one` already clips a sprite to an arbitrary row
range. Cut y to the union and round x out to 8, and the same bands are **64%
of the pixels on VGA** and 70% on Hercules. On CGA, where the tile is 8x4, the
rounding costs almost nothing (90%) - the tile is already about the size of
the step.

**A three-tile band is the ghost's own sub-tile phase, and a dropped frame
makes it worse.** A ghost moves at 88%, so its box lands at any phase within
the tile and a union that spans three columns is ordinary. The comparison that
shows the compounding is Hercules against VGA at the same tile width: 48-wide
bands are **6.6% there and 13.4% here**, and the difference is the fifth of
VGA frames that arrive covering two ticks.

## The OS-side bracket

VGA, per frame, measured on the kernel's own symbols:

| | ms | note |
|---|---|---|
| `gfx_lock` | 0.11 | the fast path, as designed |
| `wm_clip_set` | 3.11 | the occlusion walk, every frame |
| `dd_overlay` | 0.10 | a compare and a return while playing |
| `gfx_unlock` | 3.22 | of which `fpg_finish` 0.085 and `vid_ctx_act` 0.098 |

**6.3 ms - 13% of the VGA frame - is kernel code that the package asks for
once a frame and cannot skip.** `wm_clip_set` recomputes the visible region of
a window nothing has moved. Both are 1.8-1.9 ms on the 1bpp adapters, which is
the same routines with a quarter of the VRAM traffic.

**The unlock's remainder is the mouse arrow, and it is redrawn every frame
wherever the arrow is.** `gfx_blit1_x` (kernel/vga12.inc) spends the deferred
hide with no position test at all -

```
    cmp byte [cur_lazy], 0
    je .lazyok
    call KERNEL_SEG:cw_cur_unlazy
```

\- so every band blit hides the arrow and `gfx_unlock` then shows it again.
Measured with the pointer parked at (4,4), on the desktop, well outside the
window: **`cursor_show` is reached in 165 frames of 165**, and the span is
3.20 ms against 2.71 with the pointer in the middle of the board. The
comment's reason - *"this may write any pixel on the screen"* - is true of an
unclipped call and not of this one: a background painter reaches
`gfx_blit1` with a region armed, and `cur_lazyck` is the position-aware form
the other primitives use. It would cost nothing when nothing overlaps.

## `dd_step`

VGA, per frame, at 1.61 steps a frame in this sample:

| call | per frame | per call | share |
|---|---|---|---|
| `dd_act_move` x5 | 6.55 | 0.496 | 55.8% |
| `dd_gh_think` x4 | 2.02 | 0.327 | 17.2% |
| `dd_input` | 1.50 | 0.929 | 12.8% |
| `dd_anims` | 0.74 | 0.461 | 6.3% |
| `dd_collide` | 0.45 | 0.281 | 3.8% |
| `dd_release_ck` | 0.19 | 0.118 | 1.6% |
| `dd_step` header | 0.17 | 0.103 | 1.4% |
| `dd_modes` | 0.07 | 0.045 | 0.6% |
| `dd_fruit_tick` | 0.05 | 0.028 | 0.4% |

`dd_input` is 0.93 ms to read four keys, which is four `OSAPI_KEY_DOWN` far
calls where 46.7 us each would be 0.19.

## What this says to go after

Ranked by measured size, not by how interesting the code is.

1. **Cut the band's y to the union instead of out to the tile grid** - ~36%
   of band pixels on VGA. It reaches the blit, the ground copy and both sprite
   composers, which are 21.5 of the 29.9 ms. `dd_band_items` is the one that
   needs new code: a dot whose tile is only partly in the band has to be
   clipped, which `dd_band_one` already does for a sprite.
2. **`dd_actor_prep` does four 16-bit `div`s per actor** to find the tile
   range - twenty a frame, at ~160 cycles each, dividing by a constant that
   is fixed for the whole level.
3. **`gfx_blit1_x` asks `cur_unlazy` where it could ask `cur_lazyck`** - up
   to 3.2 ms on VGA, for a pointer that is not over the window. Kernel, a
   handful of bytes, and it pays every package that puts up a band.
4. **`wm_clip_set` at 3.11 ms** recomputes an answer that changes only when
   a window moves, resizes or restacks. Kernel, and it would pay every
   real-time package.
5. **`dd_act_move` at 0.496 ms a call** is 13% of the whole frame across five
   actors.
6. **The corner repair at 2.63 ms** is the thing being weighed, and it is
   sixth on this list rather than first.
