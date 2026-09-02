# Handoff: the mixed-depth drag freeze (docs/DUAL-DISPLAY-VGA.md §8(8))

> ## THE FREEZE NO LONGER REPRODUCES. Read this box before the rest.
>
> Merging `elendilon` (`0ca93d6`) took §2's one-command reproduction from a
> **hard freeze** to a live machine. The same command reaches the same point
> in the same script — `secondary vga at x=720`, then the drag — and now
> fails in the *harness* with the guest answering: `os88mouse` reads
> `mouse_x` back and reports `could not reach (1100,205): stuck at
> (979,205)`. A frozen machine cannot answer that, and §1's own test says so:
> the BIOS tick at `0040:006C` is running.
>
> **The likely fix is `1b3691f` (§39.14), and it names this bug's call
> stack.** `gfx_xor_rect_sw`, `gfx_save` and `gfx_restore` now bank AX–DX
> around `GFXDENTER`/`GFXDORG` — *"the hook translates the rect into
> display-local coordinates and nothing puts it back, so three primitives
> documented `out: -` were answering a rect on a two-card machine."*
> `gfx_xor_rect_sw` is the frame §4's captured stack dies under, and a rect
> silently translated into another display's coordinates is exactly a write
> through the wrong context — §4's segment-zero signature.
>
> **What is NOT yet done:** nobody has driven the original *manual* session
> (press the title bar, walk right in 100px packets across the seam) on the
> merged tree and watched it survive. Do that before closing this, and if it
> survives, delete this file rather than editing it.
>
> **What is left is a different thing and belongs in §8 of the plan, not
> here:** with the mono card primary, a drag cannot take the pointer past
> **x = 979** on a desktop that runs to 1359. It is not `mou_clamp` — with
> the **button up** the pointer reaches 900, 1000, 1100 and 1300 exactly, on
> the same arrangement (`d0` Hercules 720×348 at (0,0), `d1` VGA 640×480 at
> (720,20)). So it is a clamp in the drag path: `ui_drag`, `ui_drag_dead`'s
> seam zone, or `wm_fit`.

**Everything below predates that merge** and is kept because the ruled-out
list and the instrument notes are still worth having. Written in the shape
docs/FIELD-NOTES.md uses: what it is, how to reproduce it in one command,
what has been *ruled out* with a measurement, what is established, and what
has already been tried and reverted so it is not tried again.

Nothing in this file is speculation unless it says so.

---

## 1. The symptom

On a machine with **two displays of different colour depth** — a VGA beside a
Hercules, which is the only such pairing that exists — dragging a window across
the seam **hard-freezes the machine**. The BIOS tick at `0040:006C` stops, so it
is a freeze and not a stall.

It does **not** happen on Hercules+CGA, in either primary direction. Those two
displays are both 1bpp.

## 2. Reproduce it

```sh
make marty && make                       # the shipped kernel; no knob needed
python3 tests/dispsave.py --machine os8088_xt_vga_herc --swap
```

`--swap` makes the **mono card the primary** after extending, which is the
configuration that dies. It reaches the arrangement (`secondary vga at x=720`)
and then freezes in the drag.

The same thing by hand, which is what every measurement below used: boot
`os8088_xt_vga_herc`, open the Control Panel's Display page
(`tests/dispcp.open_panel`), click **Right**, click the **Hercules** adapter row
and **Activate** (`dispcp.set_primary(..., dispcp.adapter_row(avail, 1))`), then
press the panel's title bar and walk the pointer right in 100px packets. It
dies on the packet that crosses x = 720.

## 3. What is RULED OUT, each with a measurement

| ruled out | measurement |
|---|---|
| the pointer clamp (§39.15.4) | the pointer reaches (1100, 205) with `[cur_disp]` flipping 0→1, both button-up **and button held over bare desktop**. It is only a drag that dies. |
| `vid_apply` re-homing the cursor | a counter on `vid_apply` plus the caller's return address: it does **not run during the drag** (flat at 8 across every packet). |
| the interrupt vector table | `int 08` reads `0060:4741` before **and** after the freeze; 3 bytes differ in the low 256, all in the `int 13h` area. |
| a task-stack overrun | at the freeze `SP` is **22 bytes** from its healthy value with `SS`/`DS` correct, and `sch_stkdie` (`0060:4851`) is never entered — the canary did not fire. Task 0 has 1024 bytes, not 256. |
| a poisoned `vid_ctx` record | read after the primary swap: d0 = B000/90/720×348/kind 1, d1 = A000/80/640×480/kind 0, live = mono 1, stride 90, rseg B000. All correct. |
| an unbalanced cursor display bracket | `cur_dnest` (a nest counter added for the experiment) reads **0** at every observation, including after crossing to display 1 and back. `CUR_DBEGIN`/`CUR_DEND` **are** paired. |

**The "cursor home position" lead was a measurement error and is closed.** The
pointer appearing to stick at (320,240) and (680,250) — the centres of 640×480
and 1360×500 — was a **torn 16-bit read** of `mouse_x`: 719 → 819 caught as the
new low byte `0x33` with the old high byte `0x02` = 563. Read a live two-byte
counter twice before believing anything it implies.

## 4. What IS established

**The proximate cause is a write through segment zero, and its signature is
exact.** Across one drag, **886 bytes of segment 0 change**, and:

- the commonest gap between changed addresses is **80** — the VGA's stride;
- the write goes through `[vid_rseg]`, which is **0** — the VGA's value;
- the values change by XOR-shaped single bits (`89→88`, `00→87`), which is the
  drag outline, an XOR overlay.

So the **software** renderer (which is the only thing that uses `[vid_rseg]`)
is running against an otherwise-consistent **VGA** context. `[vid_mono]` alone
disagrees with the rest of the live block.

It XORs the outline into the kernel's own code — **150 of one 8 KB `.text`
span** — and the machine dies later and somewhere else, when something
far-calls through what was overwritten. At the freeze the registers read
**`CS = 0, IP = 0x68, IF clear`**: the CPU executing the vector table as code
with interrupts off.

**One call stack was captured before the first candidate fix** and names the
path directly:

```
ui_task.drag → ui_drag.linger → ui_drag_xor → vga_xor_rect_vram
  → gfx_xor_rect_sw          ← the MONO arm, so [vid_mono] was 1
  → sw_xor_rect → sw_xor_fill → sw_rect.go → sw_plane_op
  → CALL FAR 01CC:001E       ← gone
```

`m.cmd(cmd="callstack")` and `m.cmd(cmd="regs")` are how those were taken;
neither is wired into `os88marty.py`'s CLI, so call them through `cmd`.

## 5. Two structural facts that make the state reachable

Both are true of the shipping kernel and neither is a bug on its own:

1. **`vga_xor_rect_vram` is the only drawing call in the kernel that takes
   VIRTUAL coordinates and has no display hook** — no clip, no translation, no
   display selection. It is a transient overlay and bypasses §11.3 on purpose.
   Its one caller is `ui_drag_xor`.
2. **`ui_drag` is the only place that draws across many passes inside a single
   `gfx_lock` hold.** §39.14.3's contract is that a hooked primitive leaves the
   display it drew on **current**, and that **`gfx_unlock` is what puts display
   0 back**. For the length of a drag that restore point never arrives.

Together: whatever the cursor crossing the seam last activated is still live on
the next drag pass, and the outline is then handed to it with the desktop's
coordinates still in it. **On two 1bpp displays this is invisible** — the live
words differ, the *renderer* does not, so drawing through the wrong context
still puts mono pixels in a mono framebuffer.

That is the best available theory. **It does not yet explain the measurement**,
because pinning the display (§6) did not stop the segment-zero writes.

## 6. What has been tried and REVERTED — do not repeat these

| attempt | what happened |
|---|---|
| `GFXDISP` on `vga_xor_rect_vram` | cured the Hercules-primary freeze, **broke the VGA-primary direction**. `gfx_disp_run` keeps its rect and body pointer in **module scratch**, is not re-entrancy-guarded against a hooked body (its guard is `[gfx_dnest]`, which it never increments), and does not restore the display either. |
| pin display 0 in `ui_drag_xor` (`vid_ctx_act` AL=0) | tried **twice**, before and after the other fixes. Passed a synthetic packet walk; **did not stop the segment-zero writes** (107 bytes still corrupted) and did not stop the freeze. |
| translate the outline by `[vid_ox]`/`[vid_oy]` | no change at all. |
| `pushf`/`cli` … `popf` around `vid_ctx_act`'s publish | closes a race that the §4 call stack **directly evidences** (the mono arm with `rseg` 0), and **broke `dispsave` on the VGA-primary machine**. Committed as `a371ad5` and reverted in `041e14f`. |
| `cur_dprev` → a nest-indexed save | **broke `dispsave` on VGA+Herc**: no raise cache is taken for a window on display 1, deterministically. Reason unknown. Not in the tree. **← THIS VERDICT IS NOW IN DOUBT, see §7.** |

## 7. A real defect found on the way, still unfixed

**`CUR_DBEGIN`'s comment says the outgoing display is "banked on the stack". It
is banked in one global byte, `cur_dprev`** — and the header three lines above
it names the two contexts that then collide: the bracket is taken on the UI task
by `gfx_lock`'s deferred hide **and inside IRQ4**. An interrupt landing in the
task's bracket overwrites the outer saved value, so the task is left on the
cursor's display:

```
UI task   bank 0, make display 1 current
  IRQ4      bank 1        <- overwrites the 0
  IRQ4      restore 1
UI task   restore 1       <- left on the WRONG display
```

The bracket **is** balanced (§3), so this needs genuine nesting to bite. It is
harmless while every display agrees about depth.

**The reason it was dropped has fallen over, and re-testing it is the first
thing to do here.** It was dropped for breaking `dispsave` on VGA+Herc
"deterministically, reason unknown" (§6). That gate flips its verdict on
**dead bytes**: at `bf83158`, `times 100 db 0` added to `.text` — padding
nothing can execute — takes it from PASS to the identical *"no raise cache
was taken for a window on display 1"*, 2/2 against 2/2 with the images
rebuilt clean before every run. The `cur_dprev` fix adds bytes. **The
evidence against it is not evidence.**

**The cause is known now and it was a real kernel bug**: `elendilon`'s
§11.96.11.2 (`82cf28c`) — `wm_su_ext`, four band-extent bytes **per window
SLOT**, never cleared at `wm_destroy`, so a window in a reused slot inherited
the last tenant's bands. Merged in, the gate passes. Which slot a window gets
is a property of the *session*, which is why a build that diverges anywhere
earlier lands on a different answer, and why dead bytes could flip it.

Two things about that gate before anyone leans on it again
(docs/DUAL-DISPLAY-VGA.md §8(9) is the full account):

- **`dispsave` dirties the disk it boots** — `close_panel` writes
  `SYSTEM.CFG` — so no repeat run is comparable to the first without
  `rm -f build/os8088*.img build/apps*.img && make` in between.
- Its failure text *names* `wm_su_take`'s gate as the cause. That is a
  hypothesis it prints, not something it measured, and it sent the
  investigation at the wrong subsystem twice.

## 8. Where to start

1. **Find the writer, do not reason about it.** Everything above is inference
   from *state*; nobody has yet caught the instruction that writes segment 0.
   MartyPC's debug server has `step` and breakpoints
   (`tools/martypc/debug_server.rs`, `"step"` and the breakpoint commands) and
   neither is exposed by `os88marty.py` — wiring one up and stopping on a write
   to a low address is the measurement this bug has been missing all along.
2. **`[vid_mono]` is the one word that disagrees.** Whoever sets it to 1 while
   the rest of the live block is the VGA's is the bug. It is written in exactly
   two places: `vid_depth_set` (called by `vid_apply` and by `vid_ctx_act`) and
   nothing else. `vid_apply` is proven not to run (§3).
3. **Instrument, do not fix.** Five candidate fixes have now each traded one
   failure for another. A sixth guess is worth less than one breakpoint.

## 9. The gates that bind

**Byte identity on the three single-card adapters is not enough and will pass
anything** — a single-display machine never calls `vid_ctx_act` at all. That is
how `a371ad5` was committed with a regression in it.

Run all of these before committing anything in `kernel/vidsel.inc`,
`kernel/mouse.inc`'s display bracket, or the `gfx_*` display hooks:

```sh
python3 tests/dispsave.py                                    # Hercules+CGA
python3 tests/dispsave.py --machine os8088_xt_vga_herc       # VGA+Hercules
python3 tests/dispsave.py --machine os8088_xt_vga_herc --swap
python3 tests/dispmode.py
python3 tests/dispmode.py --machine os8088_xt_vga_herc
python3 tests/dualcheck.py
python3 tests/dualcheck.py --machine os8088_xt_vga_herc
```

plus the framebuffer-hash A/B on `os8088_5150_cga_gla`,
`os8088_5150_herc_gla` and `os8088_xt_vga`.
