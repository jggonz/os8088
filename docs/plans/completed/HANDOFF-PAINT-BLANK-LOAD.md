# HANDOFF — a picture that does not GROW Paint's window is never painted

**Status: CLOSED. SPEC.md §11.90.3.2 is the fix and the contract; this file
is the diagnosis behind it**, kept because the diagnosis was most of the work
and because two of its own conclusions were wrong in ways worth not repeating.
Everything below was measured on a cycle-accurate 5150 under MartyPC, on
**both** CGA and VGA, against the tree at the commit that added SPEC.md §24.6's
category disks.

It was found by accident: `MEDIA/SAMPLE.BMP` on the new office disk (§24.6.2)
was drawn 448 wide *because* 448 is `PT_CW_DEF`, and that turned out to be the
one width that did not work. The sample shipped at **466** for one round as a
recorded workaround, and is **back at 448** now (§24.6.2.1).

**WHAT IT WAS.** §11.90.3.1 lets `wm_resize` answer `wm_damage` with the EMPTY
rect — x1 = 1, x2 = 0, which §11.90.2 documents as *draw nothing* — for a
window that did not grow and whose origin did not move. That is correct: the
kernel painted over nothing that survived. Its safety argument enumerated
Paint's `OSAPI_WM_RESIZE` call sites and found every one content-*preserving*
(the size boxes, the full-screen exit). §54.10 then added two that are not:
`pt_onwake` and `pt_ondlg` resize the window **after** a load has replaced the
entire canvas. `pt_blit_dmg` was handed the empty rect and correctly drew
nothing over a canvas whose every pixel was new. The fix is the app's, which
is what §11.90.3.1's own last paragraph said it would have to be: `pt_adopt`
raises `[pt_cvnew]` and `pt_dmg_get` spends it. 22 bytes of image, 1 of bss,
kernel byte-identical.

**TWO OF THIS DOCUMENT'S OWN CONCLUSIONS WERE WRONG**, and both are corrected
in place below rather than deleted, because each was arrived at honestly from
a table that really did sort that way:

1. **§6's discriminator is wrong.** It is not `[pt_cw]` changing; it is *the
   window did not GROW*, by either axis. A **300**-wide picture changes the
   width, shrinks the window, and is blank — the row that was never taken, and
   the one that would have settled it. §6's own VGA observation (a height
   change, still blank) already contradicted the width theory and was recorded
   as a puzzle rather than read as a refutation.
2. **`PT_CW_MIN` is 50, not 448** (`WMIN_W - PT_CHROME_W`). A 440-wide picture
   gives a **440** canvas; it failed by shrinking the window, not by being
   clamped back up to 448. That wrong constant is what made the table look like
   a statement about 448 specifically.

**§7's shortlist was right about where to look and wrong about the order.**
Item 1 — instrument the screen path, not the load path — is what finds it in
one reading: `pt_dall` is 0 with an empty rect in the failing case and 1 in
the working one, and adding the content origin back to that rect gives the
kernel's own (1,0)..(0,0) sentinel, which names `wm_damage` directly. Items 3
and 4 (`pt_layout`'s per-width state, `[pt_cols]`) were the width theory's
children and had nothing in them. §9 was right that it reaches File > Open:
confirmed on the glass, blank before the fix and drawn after.

`tests/paintnogrow.py` is the regression row, on CGA and VGA.

---

## 1. The symptom, in one sentence

Open a BMP whose width leaves Paint's canvas width **unchanged**, and the file
decodes perfectly into the canvas and is then **never drawn on the screen**.
The window sits white. Nothing about it looks like a failure: the toast says
`Opened`.

`PT_CW_DEF` is 448, so on CGA **every picture 448 wide or less** is in this
class on a fresh Paint — though not for the reason given here: `PT_CW_MIN` is
**50**, and a narrower picture gives a narrower canvas that fails by SHRINKING
the window. That includes the width a picture *saved out of a fresh Paint*
has, so it is also the width that will not come back.

## 2. Reproduce it in two minutes

```sh
python3 - <<'PY'
import sys; sys.path.insert(0, "tools")
import os88sample as S
S.W = 448                       # the shipped sample is 466; this is the bug
open("/tmp/S448.BMP", "wb").write(S.bmp1(S.draw()))
PY
python3 tools/os88disk.py -o /tmp/probe.img --size 360 \
        build/paint.o88 MEDIA:/tmp/S448.BMP --folder SYSTEM/APPDATA
```

```python
import sys; sys.path.insert(0, "tools")
import os88ui, os88marty
with os88ui.boot("build/os8088-360.img", apps="/tmp/probe.img",
                 machine="os8088_5150_cga_gla") as ui:
    ui.open_drive("B"); ui.open("MEDIA")
    ui.open("S448.BMP")          # .BMP is associated -> Paint launches on it
    ui.settle()
    w, h, px = ui.m.fbuf()
    os88marty.write_png_rgb("/tmp/blank.png", w, h, px)
```

The window is white. Change `S.W` to 456 or 466 and it draws.

**`ui.settle()` before reading anything.** `ui.open` returns when the WINDOW
appears and the document read happens after it; a state read taken straight
after `open` shows `pt_fsz = 0` and looks like a file that was never opened.
That cost a wrong diagnosis once already.

## 3. The width table — the whole of the evidence

Every row is the same drawing through the same generator, only `W` differing.

| picture | canvas after `pt_adopt` | on screen |
|---|---|---|
| 448 x 110, 1bpp | 448 — unchanged | **blank** |
| 448 x 110, **4bpp** | 448 — unchanged | **blank** |
| 440 x 110, 1bpp | ~~448 — `PT_CW_MIN` clamps it back up~~ **440** | **blank** |
| *300 x 110, 1bpp — the row never taken* | *300* | ***blank*** |
| 456 x 110, 1bpp | 456 | draws |
| 466 x 110, 1bpp | 466 | draws |
| `build/OS8088.GIF`, 466 x 110 | 466 | draws |

## 4. What the guest says while the screen is white

Read out of the package segment with the symbols from a re-assembly of
`apps/paint/paint.asm` (`tests/atblit.py`'s `pkg_syms` trick), for the 448
1bpp case on CGA:

```
cw 448  ch 110  stride 56  cols 448  1bpp 1  planar 0  base 7744  pw 448
pt_bw 448  pt_bh 110  pt_bpp 1  pt_bstr 56  pt_boff 62  pt_fsz 6222
toast: "Opened"
```

Every one of those is **right**. `pt_fsz` is the UNPACKED size, so the
transparent lz4 read (§20.13.3.1) is not involved either — and the probe disk
above ships the file raw anyway.

**And the canvas holds the picture.** Reading each row at
`pt_rowseg[y]:pt_rowoff[y]` — note `pt_rowseg[]` is an **absolute** segment,
not a delta from `[pt_base]`; adding the base reads 130 KB into nothing and
looks like a wild row table:

```
row   0: seg 8129 off  6   56/56 bytes non-0xFF   000000000000...  (frame top)
row   1: seg 8125 off 14    2/56                  7fffffffffff...  (frame sides)
row  40: seg 7989 off  6    2/56                  7fffffffffff...
row  55: seg 7936 off 14   27/56                  7fff7ff7ffbf...  (the strip)
row 109: seg 7747 off 14   56/56                  000000000000...  (frame bottom)
```

That is exactly the drawing. `pt_bmp_row`, `pt_line_put` and `pt_layout` all
agree with each other. **The decode is not the bug.**

## 5. Ruled out, and on what evidence

* **Depth** — a 4bpp copy of the same picture at 448 is blank the same way.
* **Row padding** — 440 pads by one byte and fails; 456 pads and draws; 448
  needs no padding at all and fails. Padding does not sort the table.
* **The adapter** — CGA (`os8088_5150_cga_gla`) and VGA (`os8088_xt_vga`)
  behave identically.
* **Compression** — the probe disk carries the file raw.
* **My file** — `pt_bw`/`pt_bh`/`pt_bpp`/`pt_bstr`/`pt_boff` all parse right,
  and the same generator at 456 and 466 draws.
* **The decode** — §4 reads the picture back out of the canvas.
* **An ordinary missing repaint** — a `raise_window`, a cover-and-uncover of
  the window, and a genuine `move_window` all leave it white. Whatever the
  screen path reads, it is not the canvas §4 read.
* **A memory regrow** — worth writing down because it is the obvious theory
  and it is WRONG. `pt_cvgrow` only calls `OSAPI_MEM_REGROW` when
  `pt_paras` exceeds `[pt_smaxp]`; a fresh CGA Paint claims 448x110 PLANAR
  (stride 224, 1,540 paragraphs) and a 466x110 **1bpp** picture needs 413, so
  466 takes `jbe .ok` and regrows **nothing** — and draws. So the
  discriminator is not the claim.

## 6. What the discriminator actually is — WRONG, see the header

**`[pt_cw]` changing.** Nothing else in the table sorts it, and one further
measurement pins it: on VGA a fresh canvas is 448x**280** and the 448x110
picture changes the **height** — 280 to 110, a real change, and the window
visibly shrinks — and it is still blank. So a height change does not buy
whatever a width change buys.

So: something on the canvas-to-screen path is refreshed only when the canvas
WIDTH moves, and the load path leaves it stale otherwise.

> **This conclusion is wrong**, and the paragraph above it is why: the VGA
> reading — a real height change, window visibly smaller, still blank — is a
> REFUTATION of the width theory and is recorded here as a puzzle instead.
> The rule is *the window did not GROW*, and the row that would have shown it
> in one boot is a picture NARROWER than the default: 300 x 110 changes the
> width, shrinks the window and is blank. §5's list of things ruled out is
> sound; this section is where the evidence stopped being followed.

## 7. Where to look next

Nothing below has been tried; this is the shortlist a session should start
from, cheapest first.

1. **Instrument the screen path, not the load path.** The load path is proven
   correct (§4). Put a counter in whatever blits the canvas into the window
   and read it after a 448 load and after a 466 load: the interesting answer
   is "it ran and drew white", and it is a different bug from "it never ran".
2. **`pt_wchg` and who services it.** `pt_wfollow` sets `[pt_wchg]` and sizes
   the window; the actual repaint is somebody else's. If the damage is
   derived from a size DELTA, a load that changes neither dimension (CGA:
   448x110 into 448x110) generates none — but that does not explain VGA,
   where the height moved, so the delta would have to be width-only.
3. **`pt_layout`'s per-width state.** `[pt_stride]`, `[pt_bpr]`, `[pt_ushf]`
   and the row tables are all rebuilt unconditionally, so if one of them is
   *cached* somewhere else — a window-side copy taken at `pt_geom` time and
   refreshed on resize — that copy is the suspect.
4. **`[pt_cols]`.** `pt_adopt` sets it to `min(pw, cw)` at the very end, after
   `pt_wipe`. It reads 448 in the failing case, which is right, but it is the
   one variable in the tail whose only job is to bound a draw.
5. **Bisect the width.** 448 fails and 456 draws. Both are multiples of 8 and
   both leave `pt_cols` equal to `pt_cw`. 449..455 will say whether the
   boundary is "the width changed at all" or "the width crossed 448".

## 8. Two things to be careful of when working on this

* **`ui.settle()` before every state read** — see §2. This is the single
  easiest way to spend an hour on a non-bug.
* **`pt_rowseg[]` is absolute** — see §4. Adding `[pt_base]` gives plausible,
  self-consistent, entirely wrong numbers: the deltas between rows still come
  out at exactly `stride`, so the table looks right and reads garbage.

## 9. What it is worth

More than the sample it was found by. 448 is the default canvas width, so it
is the width of anything drawn in a fresh Paint and saved — which means the
round trip *draw, Save As, reopen* is in this class. That has not been
confirmed on the glass (it wants a File > Save As drive through the Standard
File dialog) and is the first thing to check after §7.1, because if it holds,
the bug is not about opening foreign pictures at all.
