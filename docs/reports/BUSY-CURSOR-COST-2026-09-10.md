# What an hourglass pointer costs, in kernel bytes and in guest cycles

**A measurement, not a description.** Taken 2026-09-10 on a four-core cloud
container — `nasm` 2.16.01, MartyPC at the pinned commit, guest figures off
`os8088_5150_cga_gla` (a 4.77 MHz 8088 with a CGA). It is true of the tree it
names and of no other; a later measurement is a new file.

The question it answers, as it was asked:

> Investigate adding another cursor style, an hour glass, that would be
> displayed when the system is inside a file progress lock and unable to be
> interacted with. This should not show during a standard window lock by
> default, as it would flicker back and forth too much - but a window should be
> able to ASK it to be set with their lock. How much would this cost in kernel
> bytes? Is there a noticeable performance impact?

The design is SPEC.md §7.5. The two headline numbers are **173 bytes of
`.text`, identical on both kernels**, and **0.0045% of the CPU at the worst
rate measured**.

## 1. The baseline, and a trap in taking it

`tools/kernsize.py` **re-assembles the kernel** to measure it, so it reads the
tree and not `build/`. A first baseline taken while a `make -j4` was still in
flight read `text 49,207 / cold 39,219`; the pristine tree is `text 49,218 /
cold 39,220`, and every delta below is against that. The 11-byte error would
have been quoted as part of the feature's cost.

| | `.text` | `.bss` | `.cold` | `.lowbss` |
|---|---:|---:|---:|---:|
| `kern_big` before | 49,315 | 6,016 | 39,237 | 9,182 |
| `kern_big` after | **49,491** | 6,016 | 39,237 | 9,182 |
| `kern_small` before | 37,261 | 4,242 | 26,169 | 5,460 |
| `kern_small` after | **37,437** | 4,242 | 26,169 | 5,460 |

Taken on `elendilon-next` at `099d308`, in a worktree of the pristine commit —
the figures are the same 176 on the branch this was first built against, whose
base was `801030d` and read 49,218 / 37,261.

**+176 `.text` on each, and nothing anywhere else.**

## 2. Where the 176 go

Every row is a symbol span off `tools/os88sym.py` against the built image, not
a count of source lines.

| piece | bytes |
|---|---:|
| the picture — two 12-byte tables plus `cur_shtab`/`cur_shhot` | 28 |
| `cur_busy_on` — three arms, four refusals | 71 |
| `cur_busy` — the package's door, and the lock test in it | 20 |
| `cur_busy_take` (18) + `cur_busy_undo` (14) + `[cur_shprev]` (1) | 33 |
| the API table's 167th slot | 8 |
| `fpg_arm`'s two calls | 6 |
| `gfx_unlock`'s compare, branch and call | 10 |
| **total** | **176** |

Measured variants, each built and read the same way:

| variant | `.text` |
|---|---:|
| the picture alone — bitmap, tables, constants, no mechanism | +28 |
| the file-operation half only, restore under `fpg_finish`'s own gate | **+67** |
| the whole thing | **+176** |

So the package-facing verb — the slot, its door, `cur_busy_on`'s extra arms and
the `gfx_unlock` compare it needs — is **109 of the 176**, and it is separable.

**The rung, and it is somebody else's number.** On `elendilon-next` nothing
crosses: the base has **477 bytes** of image-rung headroom (`accrued image
35/512`), so `KERN_SIZE` stays 110,592 on `kern_big` and 75,776 on
`kern_small`. The identical 176 bytes DID cross one on `801030d`, which had
**62** left. Same change, same bytes, two different rung answers — which is
CLAUDE.md's banner working: quote the byte, never the step.

## 3. The picture costs the renderers nothing on 1bpp and 1.04x on VGA

`cur_put_mono`/`cur_get_mono` walk `[cur_rows]` rows of `CUR_SPAN` bytes with
no test on the bits, so **the 1bpp cost is identical for every shape** — that
is §7.2's claim, checked in the source rather than taken on trust.

`cur_draw` is not blind: it skips a framebuffer byte whose shifted white row is
empty (`or ah, ah / jz`), so a denser picture writes more. Counted over all
eight pen phases, bytes written per draw:

| pen phase `x & 7` | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | mean |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| arrow | 12 | 15 | 20 | 21 | 22 | 22 | 23 | 23 | 19.75 |
| hourglass | 11 | 22 | 22 | 22 | 22 | 22 | 22 | 22 | **20.62** |
| delta | −1 | **+7** | +2 | +1 | 0 | 0 | −1 | −1 | **1.04x** |

Worst at one phase, cheaper at three, and `cur_saveu`'s four planes — which are
shape-blind — dominate either way.

## 4. §7.4.3.1's block fired ZERO times, and that redirected the design

The first placement put the swap inside `fpg_arm`'s §7.4.3.1 re-show, where it
would have been free. Counters at each of that block's five tests, one session
— mount, directory walk, package launch:

| test | arms reaching it |
|---|---:|
| an fsx bracket is armed | 3 |
| the gfx lock is held | 3, and **2 fell out here: it was FREE** |
| `[cur_level]` is exactly −1 | 1, and **it fell out here** |
| no clip region | 0 |
| the pointer is below the menu bar | 0 |
| **the re-show, and the swap with it** | **0** |

§7.4.2.1 says why in as many words: *a package launch and an assoc open both
read with the lock FREE*. The arrow is simply lit there and the ISR is tracking
it, so there is no hide/show pair to change the picture inside — which is why
`cur_busy_on` has a lit arm that buys one, at one cell erase and one cell draw
**per freeze**, against a freeze whose floor is `FPG_WARM` = 3 sectors (~72 ms
on the field machine) and which is usually seconds.

## 5. The one hot path, priced

`gfx_unlock` gains `cmp byte [cur_shape], CUR_BUSYSH` and a `jne` — 7 bytes,
~30 cycles with the 8088's `max(clocks, 4.34 x bytes)` fetch floor. A temporary
`inc word [gfx_ucnt]` in `gfx_unlock`, read against MartyPC's cycle counter:

| what the machine was doing | unlocks/s | the added compare, as a share of a 4.77 MHz 8088 |
|---|---:|---:|
| idle desktop, 10 guest seconds | **0.0** | 0.0000% |
| idle with a Disk window up | 0.2 | 0.0001% |
| opening a folder | 1.5 | 0.0009% |
| launching a package | 4.1 | 0.0026% |
| **dragging a window, continuously** | **7.1** | **0.0045%** |

Zero at idle is §8.1.2 working: a blocking `ui_task` draws nothing, so it takes
no lock. The counter was removed before the tree was committed.

Everything else the feature does is **per freeze**: two `cur_shape_set` calls
(~30 instructions each), and on the lit arm the erase/draw pair above.

## 6. What the scoped soak found

Two defects, both in this work, both invisible to the fast tier:

- **`curdisk`** — the `fpg_arm` call sat OUTSIDE `%ifndef NOCURDISK`, so the
  knob build that exists to measure the pre-§7.4 freeze had a pointer put back
  on the glass during it: *"NOCURDISK=1 moved the arrow 2 times during the
  freeze, and it cannot"*. The call is inside the gate now (§7.5.3.1).
- **`curbusy` itself** — the row reads `build/office360.img` and did not
  declare it, so under the soak's frozen tree it died in `shutil.copyfile`
  before booting. `wants=("build/office360.img",)` is the fix, and
  docs/WRITING-TESTS.md names that exact failure.

### 6.1 …and one failure that is NOT this change: `curdisk`

`curdisk` failed the scoped soak on its `NOCURDISK=1` **folder** arm —
*"moved the arrow 1 times during the freeze, and it cannot"* — while both
default arms passed (30 and 37 moves, 79% and 83% lit). Two facts settle it,
and neither is an argument:

- **The knob arm is no longer byte-identical to the base's**, so it could not
  be waved away: HEAD's `NOCURDISK=1` kernel is 512 bytes larger with every
  address shifted by **8** — `OSAPI_CUR_BUSY`'s table slot, which is in every
  build because the API table is ABI (SPEC.md 20.8 rule 4). Nothing on that arm
  *calls* the new routines (`[cur_shape]` never leaves 0 there), but the layout
  moved, so a timing perturbation was a live hypothesis.
- **So both points were rated rather than argued.**
  `tools/os88bisect.py sample curdisk --at 099d308 --at cbd4f0b -n 6`:

| point | failures | leg |
|---|---:|---|
| `099d308` — `elendilon-next`, the base | **3 / 6** | `[folder] NOCURDISK=1 moved the arrow 1 times` |
| `cbd4f0b` — this work | **2 / 6** | the same |

  **The base fails more often than the branch.** It is the row's own
  intermittent, documented at docs/plans/SOAK-PARALLEL.md §8.8, whose
  control hopes a sample lands
  instead of provoking the collision. `classify` had already refused to bisect
  it — *INTERMITTENT 1/3 at HEAD, and a rate is not a side*.

## 7. What the gate does not cover

`tests/curbusy.py` is registered in the soak tier (30 s) and both halves go red
when broken on purpose — `fpg_arm`'s `call cur_busy_on` out, or Paint's `call
OSAPI_CUR_BUSY` out. It does **not** cover the lit arm's double-hide hazard
(`cur_shape_set`'s own `cur_unlazy` firing after an explicit `cursor_hide`,
settling `[cur_level]` at −2): breaking that on purpose still reads PASS,
because two arms of three are lock-free — there is no promise to spend there —
and their samples dominate. That one is closed by construction, in the three
bytes that spend the promise above the arm test.

## 8. On the glass

Driven with `tools/os88ui.py` on `os8088_5150_cga_gla`:

- at rest `[cur_shape]` = 0; during a mount + directory walk + package launch,
  180 of 754 samples read 2, and it is 0 again within a second of the last
  unlock — the same lifetime `[fpg_on]` itself has;
- the cell photographed off the framebuffer mid-freeze is the hourglass
  bit-for-bit — a white tile with the black glass, bars top and bottom;
- `OSAPI_CUR_BUSY` is reached from `apps/paint` on an assoc open of
  `SAMPLE.BMP` with the lock **held** (`gfx_lock_flag` = 1), the shape reads 2
  for every sample across the decode, and 0 after.
