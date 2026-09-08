# Testing the Hercules renderer

It is automatable, on two instruments, and the ways of getting it wrong all
produce a black or plausible-looking image rather than an error.

- **MartyPC is the default** (docs/MARTYPC-DEBUG.md). It models a real
  MDA/Hercules on a cycle-accurate 8088, so `kernel/viddet.inc`'s detection
  probe (SPEC.md §39.1) and the 6845 programming genuinely run, and the
  card's memory is read back directly.
- **QEMU emulates no Hercules card.** `VIDEO=herc` forces the renderer and
  `HERCSEG=` relocates its framebuffer into spare RAM, where
  `tools/hercshot.py` reads it back. That verifies the software renderer and
  nothing about the card, and it is where the QMP scripted mouse lives.
- **86Box** (`make xt-hercules`) is a period ROM on real card models, with no
  automation socket: a session can start it and cannot read the result.

A bare desktop reads **55.7% lit** (139,4xx of 250,560 pixels) on both
emulators — the ground is a 50% dither plus chrome. `0.0%` or `100.0%` means
the capture is reading the wrong memory, not that the renderer is broken.

---

## MartyPC

```sh
python3 tools/os88marty.py launch build/os8088-360.img --apps build/apps360.img \
        --machine os8088_5150_herc_gla        # prints its host:port
python3 tools/os88marty.py <addr> shot herc.png    # 720x348, 55.7% lit at the desktop
python3 tools/os88mouse.py <addr> click X Y
python3 tools/os88marty.py kill <port>
```

From Python, `os88ui.boot(img, apps=..., machine="os8088_5150_herc")` is the
same machine with the UI verbs on top (`tools/os88ui.py`).

- **`os8088_5150_herc` is the IBM-ROM machine and the ROM is not in the
  tree**; `os88marty.launch()` refuses it with `ROM set ibm5150_82_v4 not
  found`. `os8088_5150_herc_gla` is its GLaBIOS twin and always boots;
  `os88ui.boot` and `os88marty.machine()` make that substitution for you,
  `launch()` does not. `tools/martypc/configs/os8088_machines.toml` has both.
- **`shot` decodes VRAM by default on Hercules**, applying SPEC.md §39.3's
  banked layout, so its PNG is byte-comparable with `hercshot.py`'s and with
  every "0 differing pixels" check in `tests/`. `shot --rendered` is what the
  card rasterised instead: 720x350, and guest (x, y) lands at (x−16, y+2) in
  it (docs/MARTYPC-DEBUG.md).
- **`screen` shows nothing useful here.** MartyPC's MDA reports a text mode
  whatever the card is doing, and `screen` prints character cells decoded
  out of a graphics framebuffer — a blank page with a few stray digits. Use
  `shot`.
- The one Hercules defect this emulator cannot show is a card wrongly left
  in **text** mode: the MDA's mode field is dead, so the boot gate never
  waits on it and a text-mode Hercules captures the same as a graphics one.
  `tests/dispherc1.py` is that case and says so; it is 86Box's or the 5150's.

---

## QEMU

```sh
# 1. build + boot with the adapter forced and the framebuffer in spare RAM
make test VIDEO=herc HERCSEG=0x7000

# 2. read the framebuffer back as a PNG - LINEAR address, see the traps below
python3 tools/hercshot.py build/qmp.sock 0x70000 shot.png

# 3. drive it - --screen is MANDATORY, see the traps below
python3 tools/mouse.py --screen 720x348 build/qmp.sock click 600 300
python3 tools/qmp.py build/qmp.sock 'sendkey h'
```

`hercshot.py` spawns `tools/qmp.py` by relative path, so run it from the
repository root; it leaves the raw 32KB dump beside the PNG as `shot.png.bin`
and prints the lit-pixel count:

```
shot.png: 720x348, 139464 lit pixels of 250560 (55.7%)
```

To keep `build/` on the shipped kernel, build the knob into a private tree
instead and point QEMU at it yourself, with the same flags as the Makefile's
`test` target and a socket of your own:

```sh
python3 tools/os88build.py build VIDEO=herc HERCSEG=0x7000   # build/trees/hercseg-video-<hash>/
```

### The four traps

Each produces a black or garbage image, or a machine that ignores every
click, and all of them read as "Hercules mode does not work".

1. **`HERCSEG` is a SEGMENT; `hercshot.py` takes a LINEAR address.**
   `HERCSEG=0x7000` and `0x70000` are the same place, and the extra zero is
   easy to drop. Mismatched, `pmemsave` faithfully dumps 32KB of whatever
   else is there: `0xB0000` reads 100.0% lit.
2. **`screendump` shows you nothing, ever.** QEMU's display is still the VGA
   device; with `VIDEO=herc` the kernel renders into `HERCSEG`, which is
   ordinary RAM, so a screendump captures the VGA framebuffer — all black at
   the desktop — and does not error. `mouse.py shot` and `tools/shot.py` are
   screendumps too. `hercshot.py` is the only way to see the output.
3. **`mouse.py` needs `--screen 720x348`.** It defaults to 640x480 and
   derives absolute position by pinning against the kernel's edge clamp
   (`[vid_wm1]`/`[vid_hm1]`); on a 720x348 guest the derivation is wrong in
   both axes, so every click misses while every `hercshot` still looks
   plausible. No error message.
4. **`B0000` is unmapped under QEMU and swallows writes silently**, which is
   why `HERCSEG` exists. Without it `VIDEO=herc` renders into nothing: no
   crash, no pixels anywhere you can read them.

### What QEMU verifies, and what it cannot

Everything the software renderer does: the stride, the bank arithmetic, the
wrap out of the last bank, the 1bpp colour reduction, and every glyph, icon,
window and dither the UI drew. A wrong stride shears the picture sideways; a
wrong bank calculation shears it into four interleaved combs.

Not covered: the detection probe (forced, not run) and the 6845 register
programming (written to ports nothing is listening on). Those run on
MartyPC, and on 86Box.

---

## Rebuilding, and not shipping a forced kernel

`VIDEO=`/`HERCSEG=`/`RTC=` are in the stamp file (`build/.video-*`), so
changing one — including dropping it — rebuilds the kernel and every image.
If you forget to drop the knobs, the next `make test` boots the Hercules
kernel on a VGA run and it reads exactly like a broken probe. Nothing in
`build/` is committed, but a forced kernel sits on the disk images until the
next plain `make`; run one before cutting anything from the tree.

Separately, QEMU mounts the system image writable and the OS writes
`SYSTEM.CFG` to it, so a Control Panel setting is remembered across boots
and `make` will not undo it — the image is newer than every input. When a
run's starting state matters:

```sh
rm -f build/os8088.img build/os8088-120.img build/os8088-720.img build/os8088-360.img && make
```

## A stale QEMU will lie to you

A previous session's QEMU keeps answering on `build/qmp.sock`. `make test`
fails with `cannot create PID file`, that line scrolls past, and every
`hercshot` afterwards shows the **old** kernel — which reads as a change that
did nothing.

```sh
ps -o pid,etime,cmd -C qemu-system-i386     # compare its age to build/kernel.bin
python3 tools/qmp.py build/qmp.sock 'quit'
```

Do not `pkill -f` it from a command line that itself mentions QEMU: `-f`
matches the killing shell, which exits 144 with nothing dead and every
command after it skipped.

---

## Reading the output

The PNG is 720x348, 1bpp rendered to 8-bit greyscale. Text is 8x8, so crop
and scale before judging anything — more so than on VGA, because the screen
is wider and the glyphs are the same size. With Pillow:

```python
from PIL import Image
im = Image.open('shot.png')
im.crop((0, 0, 360, 20)).resize((1440, 80), 0).save('bar.png')   # menu bar
```

Nearest-neighbour (`0`) matters: any smoothing turns a 1bpp dither into grey
mush and loses exactly the distinction being checked.

**Geometry is not VGA's.** `SCREEN_W`/`SCREEN_H` are VGA reference values;
the live screen is `[vid_w]`/`[vid_h]`/`[vid_stride]` (SPEC.md §39.2).
Windows, the dock strip and the drive icons sit at different coordinates
than at 640x480, so coordinates copied from a VGA test land in the wrong
place. The menu bar happens to be similar because its cells are laid out
from font metrics — a coincidence, not something to rely on.

## Forcing a mono-only failure

A menu bar that re-highlighted its title on every interaction after the
first was reported from a 128KB Hercules machine and did not reproduce on
VGA: the path it needed was `menu_drop`'s save-under claim being *refused*,
which never happens on a large heap. One temporary `jmp .noclaim` after the
`call mem_claim` in `menu_drop` (`kernel/menu.inc`) forced the refusal on
any machine, and the recipe above did the rest. Two things generalise:
**force the failure rather than hunting for a machine that has it**, and
**shoot after every step** — the first interaction was clean and only the
second onwards was wrong, which a single final screenshot cannot show.
Menus open on press and close on release, so that sequence is `mouse.py
--screen 720x348 ... to 131 8`, then `qmp.py ... 'mouse_button 1' 'sleep 0.3'
'mouse_button 0'`, then `hercshot.py`, repeated.
