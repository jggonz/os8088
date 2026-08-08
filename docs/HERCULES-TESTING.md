# Testing the Hercules renderer

**It is automatable, and this is the recipe.** The claim that it is not has
been in CLAUDE.md and has cost people time; what is true is narrower — QEMU
emulates no Hercules card, and `screendump` can never show you Hercules
pixels. Everything else works, including scripted mouse input, and the
720x348 renderer can be verified pixel for pixel without leaving QEMU.

**`make marty` is the first thing to reach for now** (docs/MARTYPC-DEBUG.md):
MartyPC models a real MDA/Hercules on a cycle-accurate 8088, so the
**detection probe** and the 6845 programming genuinely run there, `screen`
reads the card back without a screenshot trick, and none of the `HERCSEG=`
relocation below is needed. `make xt-hercules` (86Box) is the second opinion
rather than the only one — that sentence used to read "remains the only test
of the detection probe", and it has been overtaken.

This document remains the QEMU recipe, and QEMU is still where the scripted
mouse and the pixel-for-pixel framebuffer comparison live. It is about
everything above those two things, which is almost all of it.

---

## The recipe

```sh
# 1. build + boot with the adapter forced and the framebuffer in spare RAM
make test VIDEO=herc HERCSEG=0x7000

# 2. read the framebuffer back as a PNG - LINEAR address, see the trap below
python3 tools/hercshot.py build/qmp.sock 0x70000 /abs/path/shot.png

# 3. drive it - --screen is MANDATORY, see the trap below
python3 tools/mouse.py --screen 720x348 build/qmp.sock click 600 300
python3 tools/qmp.py build/qmp.sock 'sendkey h'
```

`hercshot.py` prints a lit-pixel count. Use it as the smoke test: **a booted
Locator desktop reads about 60-65% lit** (the desktop is a 50% dither plus
chrome). `0.0%` or `100.0%` means you are reading the wrong address, not that
the renderer is broken.

```
shot.png: 720x348, 156992 lit pixels of 250560 (62.7%)
```

---

## The four traps

Each of these produces a black or garbage image, or a machine that ignores
every click — all of which read as "Hercules mode does not work".

### 1. `HERCSEG` is a SEGMENT; `hercshot.py` takes a LINEAR address

`HERCSEG=0x7000` and `0x70000` are the same place written two ways, and the
extra zero is easy to drop. Mismatched, `pmemsave` faithfully dumps 32KB of
whatever else is there and you get noise or black.

```
make test VIDEO=herc HERCSEG=0x7000      →  hercshot ... 0x70000
                    ^^^^^^ segment                      ^^^^^^^ linear = *16
```

### 2. `screendump` shows you nothing useful — ever

QEMU's display is still the VGA device. With `VIDEO=herc` the kernel renders
into `HERCSEG`, which is ordinary RAM, so a QMP `screendump` captures the
*VGA* framebuffer — stale, blank, or whatever the mode-set left behind. It
will not error. **If you take one screendump and conclude Hercules is
broken, this is what happened.** `hercshot.py` is the only way to see the
output, and `mouse.py shot` is a screendump too, so it is no use here either.

### 3. `mouse.py` needs `--screen 720x348`

It defaults to 640x480 and derives absolute position by pinning against the
kernel's own edge clamp. On a 720x348 guest that derivation is wrong in both
axes, so the cursor ends up somewhere other than where you asked and every
click misses its target — while every `hercshot` still looks perfectly
plausible. There is no error message.

### 4. `B0000` is unmapped under QEMU and swallows writes silently

Which is why `HERCSEG` exists at all. Without it, `VIDEO=herc` boots a
machine that renders diligently into nothing: no crash, no complaint, no
pixels anywhere you can read them.

---

## Rebuilding, and not shipping a forced kernel

`VIDEO=`/`HERCSEG=`/`RTC=` are tracked by a stamp file (`build/.video-*`), so
changing one rebuilds the kernel. Two consequences:

- **Going back to VGA is also a change.** Dropping the knobs rebuilds; if you
  forget, `make test` boots the *Hercules* kernel on your next VGA run and it
  reads exactly like a broken probe.
- **A released image is always built knob-free.** Nothing in `build/` is
  committed, so a forced kernel cannot reach the repo — but it can sit on your
  disk images indefinitely, and a release cut from one would ship a machine
  that never probes. Reset before releasing, and any time a run's starting
  state matters:

```sh
rm -f build/os8088.img build/os8088-720.img build/os8088-360.img && make
```

The `os8088.img` removal is separate and unrelated to the video knob: QEMU
mounts it writable and the OS writes `SYSTEM.CFG` to it, so any test that
touches a Control Panel setting is remembered across boots. `make` will not
undo that on its own — the image is newer than every input, so it is skipped.

## A stale QEMU will lie to you

A previous session's QEMU keeps answering on `build/qmp.sock`. `make test`
fails with `cannot create PID file` and that line scrolls past, so every
`hercshot` afterwards succeeds and shows you the **old** kernel — which reads
as a change that did nothing.

```sh
ps aux | grep qemu-system          # compare its start time to build/kernel.bin
python3 tools/qmp.py build/qmp.sock 'quit'
```

---

## Reading the output

The PNG is 720x348, 1bpp rendered to 8-bit greyscale. Text is 8x8, so crop
and scale before judging anything — the same rule as VGA, more so here
because the screen is physically wider and the glyphs are the same size.

```python
from PIL import Image
im = Image.open('shot.png')
im.crop((0, 0, 360, 20)).resize((1440, 80), 0).save('bar.png')   # menu bar
```

Nearest-neighbour (`0`) matters: any smoothing turns a 1bpp dither into grey
mush and you lose exactly the distinction you are trying to check.

**Geometry is not VGA's.** `SCREEN_W`/`SCREEN_H` are VGA reference values; the
live screen is `[vid_w]`/`[vid_h]`/`[vid_stride]` (SPEC.md §39.2). Windows,
the dock strip and the drive icons all sit at different coordinates than they
do at 640x480, so coordinates copied from a VGA test will land in the wrong
place. The menu bar happens to be similar because its cells are laid out from
font metrics, which is a coincidence worth not relying on.

---

## What this actually verifies

Everything the software renderer does: the stride, the bank arithmetic, the
wrap out of the last bank, the 1bpp colour reduction, and every glyph, icon,
window and dither the UI drew. A wrong stride shears the picture sideways; a
wrong bank calculation shears it into four interleaved combs. Both are
unmistakable in the PNG.

What it does **not** cover: `kernel/viddet.inc`'s detection probe (forced, not
run) and the 6845 register programming (written to ports nothing is
listening on). Those need `make xt-hercules`.

## Worked example: catching a mono-only regression

The bug that prompted this file was a menu bar that re-highlighted its title
on every interaction after the first, reported from a 128KB Hercules machine
and **not reproducible on VGA at all**. The path it needed was
`menu_drop`'s save-under being *refused*, which never happens on a large heap.

```sh
# force the refusal that a 128KB machine gets for free
#   kernel/menu.inc, in menu_drop:  add `jmp .noclaim` after `call mem_claim`
make test VIDEO=herc HERCSEG=0x7000
python3 tools/mouse.py --screen 720x348 build/qmp.sock click 600 300  # → Locator
python3 tools/mouse.py --screen 720x348 build/qmp.sock to 131 8       # the File cell
for i in 1 2 3 4; do
  python3 tools/qmp.py build/qmp.sock 'mouse_button 1' 'sleep 0.3' 'mouse_button 0'
  sleep 1.5
  python3 tools/hercshot.py build/qmp.sock 0x70000 /tmp/h$i.png
done
```

Two things generalise from it. **Forcing the failure beats finding a machine
that has it** — one temporary `jmp` turned an unreproducible field report
into a four-command test. And **shoot after every step, not just at the end**:
the first interaction was clean and only the second onwards was wrong, which
is the whole diagnosis and is invisible in a single final screenshot.
