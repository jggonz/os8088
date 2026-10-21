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

## 2026-10-21 — S1, the first desktop on the VBE

**Observed.** QEMU's standard VGA, `make vesatest && python3 tests/vesa.py
--png build/vesa-s1.png`: the desktop at 1024x768, 16 bits a pixel, mode
`0117h`. The menu bar with its logo, the three titles, the date and the clock;
the 50% dither over the whole desktop; the arrow; the dock's white strip. The
dock's tiles and the desktop's drive icons are absent — both are `gfx_blit1`
bands, which S1 refuses (the band body is S2's) — so the picture is the
desktop minus its icons. The gate reads `vid_kind` 4, `vid_ext` 1, 1024x768,
`vid_planes` 16 and a clean driver row, then the screendump.

**Three things the code taught the contract.**

- The boot blob was full: 6,142 of 6,144 bytes. The attach-and-switch body
  written for it, beside `vmm_boot_x`, went to `.cold` instead — 116 resident
  bytes — and is called by kmain between `kmain_o`'s return and the blob's
  release, which is the one window where the blob's `vid_switch` is still
  there to call. The overlay checker could not see a reference into a label a
  three-argument macro pastes (`VSW_SWITCH bvs, BSWK, bvw`); it can now, and
  the one other crossing that surfaced (`bdk_apply`, a macro body defined
  under `.modc` and expanded only in the blob) is registered with its reason.
- A private stack in the driver for the `int 10h` window switch was wrong:
  two tasks preempted inside one BIOS call would share it. The mouse ISR's
  own private stack doubled to 256 bytes instead, and every other call runs
  on the task that made it.
- `drv_attach`'s contract says PRESERVE BX, and the probe spent it. The
  symptom was a refused driver with `No hardware found` and a mode table of
  ten accepted modes sitting in the freed image: the init verb went to a
  garbage row and the range check refused the far call. Both ends now keep
  the row.

**Measured** (`tools/kernsize.py`, kern_big): `.text` +409, `.cold` +215,
`.lowbss` +128, `.bss` +4; no rung crossed; `KERN_SIZE` 97,792 unchanged.
Three gates tripped and were satisfied: the hot-routine parity gate (every
hook shape is now an even number of bytes), the stack-balance walker (the two
tail trampolines are exempt, with the reason on them) and the driver memory
gate (the row's term).

![The first desktop on the VBE: QEMU's standard VGA at 1024x768, 16 bits a pixel, S1.](vesa/s1-desktop-1024x768x16.png)

## 2026-10-21 — S2, every primitive

**Observed.** The Disk window at 1024x768x16: title band, folder icons,
captions, buttons, scrollbar; the drive icons and labels on the desktop; the
dock's tile; the File menu dropped with its greyed items stippled, and gone
again with the dither whole. `tests/vesa.py` now drives that - a press on
File (a menu is press, drag, release: a click opens and closes it on the
release, which is how a run passed and the next failed), a release away, a
double-click on A:.

**Three bugs the gate caught, all the same kind.** A body that read the
frame pointer where the caller's BP - the band's stride - was meant, and
drew noise where the icons go. A text run whose pixel loop spent CX, the
cell's x, so every row after the first went to the left edge. A restore
whose sub-rect arithmetic spent AX and put the pixels back at x = 0: the
menu's box came back half ghost, which the gate's dither check at one point
missed and now checks at both ends of a row the drop covered.

**The cold body.** `gfx_blit1_x` lives in `.cold`, where no data may go, so
its hook could not carry a selector byte. It got its own far entry,
`vx_callf1`, whose selector lives beside it in `.text`; the cold stub is a
far call and a jump. One such entry per cold body, and there is one.

**Measured** (kern_big against main): `.text` +508, `.cold` +226,
`.lowbss` +128, `.bss` +4. The image rung crossed once at S2: `KERN_SIZE`
98,304 against 97,792, 2,560 spare - which is the one rung the contract
bounded the hook at, spent.

![The Disk window on the VBE: 1024x768, 16 bits a pixel, S2.](vesa/s2-disk-window-1024x768x16.png)
