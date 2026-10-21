# How VESA was built

A diary, kept as the work happened, for the website later. Observations are
separated from intentions; a measurement names the machine it was taken on.
The plan is [plans/VESA-PLAN.md](plans/VESA-PLAN.md) and the contract is
SPEC.md §119.

## 2026-10-21 — S0, the shape

**The ask.** VESA modes at 800x600 and 1024x768, auto-detected, up to 16-bit
colour, as a video driver used when the card can do it; built in phases
behind one draft PR with a checked-in plan.

**What the OS already had.** Four adapter kinds driven by one kernel, chosen
by `[vid_kind]`; two renderers — the planar VGA/EGA bodies in `vga12.inc`
and the 1bpp software renderer `softgfx.inc` — dispatched by one byte,
`[vid_mono]`, at the top of every drawing body. A geometry table, `vid_tab`,
one 18-byte row per kind, copied over a live block by `vid_apply`. A driver
layer whose drivers are files the user ticks, each with a class, a service
table and a `SYSTEM.CFG` bit. A precedent for exactly this shape:
`VMMOUSE.DRV`, a feature that lives in a file because the machine the
project is calibrated against can never use it.

**The question that decided the design.** Where does a packed-pixel renderer
plug in without the XT paying for it? Three answers were priced:

1. A third renderer in the kernel. Thousands of resident bytes on a kernel
   with 3,072 spare and an image rung with 437 left. Refused.
2. A fourth kernel build (`kern_vesa`, as `kern_emu`). Zero bytes on the XT,
   but a fourth system disk for every gate that walks "every shipped image",
   and a VESA user who boots the standard disk gets nothing. Kept as the
   owner's alternative in the plan.
3. One compare per planar body, far-calling into a driver. ~11 bytes a site,
   about twenty sites, the same shape as the dual-display hook that already
   sits two lines above each of them. Taken.

**Three facts that shaped the contract.**

- Every register is a parameter to some primitive (`gfx_blit4` uses all
  eight), so a selector cannot travel in one. The stub is `call vx_go` with
  the selector as a data byte after the call; the driver reads it through
  the return address. Eleven bytes a site, no register, flags kept.
- The mouse ISR runs on a 128-byte private stack and draws the arrow. A VBE
  bank switch is an `int 10h`. So the driver owns the cursor cycle whole and
  switches banks on a 512-byte stack of its own.
- `[vid_planes]` is already multiplied into every save-under size as "bits
  per pixel over four planes". Publishing 8 or 16 there makes the three
  existing sizers compute packed byte counts without a change.

**Not taken.** The linear framebuffer: real mode cannot address it, unreal
mode is 386 code that any segment load undoes, and this kernel reloads
segments on every task switch.

**Build state at the start.** `kern_big` 97,792 of 100,864 (3,072 spare,
6 steps); image rung 437 left, cold rung 251 left. One fast-tier row,
`installabi`, fails on a clean checkout of `main` before this branch touched
anything (it wants `build/small*.img`, which a first `make` has not yet
built when the tier runs).
