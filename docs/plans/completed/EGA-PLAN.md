# EGA-PLAN.md — 640×350 as a second setup of the planar path

Design record. It records what was considered, including the options that were
rejected. **SPEC.md §39 (and §39.24) is the current state; this is how it got
there.** Cite as `EGA-PLAN §N`.

---

## 0. Why

GitHub Issue #136: an **IBM 5160** with a genuine **IBM EGA** adapter and a
350-line monitor. `vid_detect` classes every EGA as `VID_VGA` (§39.1 steps
1–2), so os8088 boots it, sets **mode 12h**, and drives a 640×480 planar
desktop into a framebuffer the card shows only 350 lines of. The bottom 130
rows — part of the dock included — are off the tube. CGA (mode 6) works and
throws the card away.

The EGA's real high-resolution mode is **mode 10h — 640×350, 16 colours from a
palette of 64**. That framebuffer is *mode 12h's shape with a shorter height*:
planar, four planes, `A000:0000`, 80-byte stride, not banked. So this is not a
new graphics backend. It is a second **setup** — geometry + BIOS mode number +
one detection branch — in front of the `vga12.inc` primitives the OS already
has.

`docs/plans/completed/FSX-PLAN.md` already saw this coming — a footnote there reads *"vid_detect
step 2 admits EGA-class cards as VID_VGA, and an EGA sets 0Dh but not 12h/13h;
if those machines ever…"*. This is that footnote, cashed in.

---

## 1. What EGA shares with VGA, unchanged

`softgfx.inc` is the 1bpp driver; `vga12.inc` is the planar one; `[vid_mono]`
routes between them (§39.5). EGA is a colour planar adapter, so `[vid_mono]=0`
and **every `vga12.inc` primitive runs on it with no change to its body**:

- `gfx_fill / hline / vline / pixel / frame / fill_gray / fill_pat / xor_rect /
  xor_fill / blit1 / blit4 / spans / line / scroll`, and the cursor's
  `vga_save_vram` / `vga_restore_vram`.
- They touch only the Graphics Controller (3CE/3CF) and Sequencer (3C4/3C5),
  use Set/Reset, the Bit Mask, ALU-function XOR, and **write modes 0 and 1** —
  all identical on IBM EGA. No DAC, no Attribute Controller, no CRTC writes,
  no write mode 2/3, no split screen.
- `vga_rect_setup` already clips to `[vid_cw]/[vid_ch]` (§39.2.1).
- `gfx_rowbase` / `gfx_nextrow`: EGA's `bmask/bshift/wrapbit/wrapfix` are `0`
  exactly as VGA's, so both reduce to `y*80` / `add di,80` with no adapter
  test. The `vid_apply` row-address table (§39.3.1) fills correctly and, at
  348 of EGA's 350 rows, nearly never misses — a *better* hit rate than VGA's
  72.5 %.
- `vid_apply`'s derived geometry (`vid_w/h`, `vid_dock_y0`, `vid_ymax`,
  `vid_popmax`, the drive column, `vid_clk_hx`, the mouse home), `wm_fit`,
  `wm_refit`, `desk_rowcalc`, menus, the dock, damage rects: all read the live
  block, so feeding them `640×350` from `vid_tab` is the whole of the
  window-system side.
- `vid_text`'s colour arm (`int 10h AX=0003h`) restores EGA text with no
  special case — EGA has a real `int 10h` and mode 3 is standard on it,
  unlike the mono card (§39.20).

## 2. What is EGA-specific

The full list. Every item is in SPEC.md §39.24's table too; this is the same
list with the reasoning attached.

| | VGA | EGA |
|---|---|---|
| detection | `int 10h AX=1A00h` → `AL=1Ah` | DCC returns 0; `int 10h AH=12h/BL=10h` then succeeds |
| `vid_tab` row | `A000,80,0,0,80,0,0,640,480` | `A000,80,0,0,80,0,0,640,`**`350`** |
| mode set | `int 10h AX=0012h` | `int 10h AX=`**`0010h`** |
| framebuffer clear (§39.23) | manual A000 wipe, `SCREEN_H` rows | same five register writes, `EGA_H` (350) rows |
| Colour theme (§76.12) | eligible | **eligible** — `thm_set` / `cp_thm_colgrey` accept `VID_EGA` beside `VID_VGA` (§7) |

Three `vga12.inc` sites still hold the assembly-time `SCREEN_H` (480) because
"it only ever runs on VGA" (§39.3): `vga_vline_core`, `vga_xor_hline`, and the
`vid_setmode` VGA-arm clear count. The first two are over-permissive on EGA —
they would let a primitive address the invisible rows 350–479 (harmless on a
128 KB card, but wrong) — and move to `[vid_ch]`/`[vid_chm1]`; on VGA those
equal the constant, so the output is byte-identical (§39.9). The clear count
does **not** move to a runtime read — it stays a constant (`EGA_H`) for the
`SPL_RESIDENT` reason §39.23 already gives, just a different constant.

## 3. `VID_EGA` is a kind, not a flag

**Considered and rejected: EGA as a variant bit on `VID_VGA`.** `vid_apply`
copies `vid_tab[vid_kind]` verbatim into the live block; with `kind ==
VID_VGA` it always loads height 480, so the variant would need `vid_apply`
to patch the height back down — a special case in the one routine §39 works
hard to keep table-driven. A fourth `vid_tab` row costs 18 bytes and no
special case, and `[vid_mono]`/`[vid_planes]` (the bytes every primitive
actually dispatches on) are *derived* from the kind by `vid_depth_set`, which
just needs `VID_EGA` folded into its colour arm.

`[vid_kind]` is compared in ~40 places. Most are `[vid_mono]`-style questions
already answered by the derived byte, or genuinely VGA-BIOS-specific (mode
set, text restore, DAC). The conflations that would break on `kind == 3`:

- **`OSAPI_WM_PREFER` / `wm_pref` / `wm_minw` / `wm_minh`** index a per-window
  12-byte table by `[vid_kind]` as `0/1/2` (`wm.inc`). `VID_EGA = 3` would
  read one pair past the block. Fix: clamp to `VID_VGA` at the lookup. The
  package-supplied table stays three pairs — **no ABI break**; an EGA machine
  takes VGA's preferred size.
- **`desk.inc`'s pixel-aspect test** is `== VID_CGA` only; EGA is untouched
  and lays out VGA-style. 640×350 pixels are mildly tall (≈1.37:1 on a 4:3
  tube); accepted, not worth a fourth aspect case.

`OSAPI_VIDEO` returns `DL = 3`, `DH = 4`. The standing `apps/os88api.inc`
guidance — branch on bpp / `[vid_mono]`, never on raw kind — is what makes
this additive; §39.8 now says so in as many words, and the `apps/` tree is
audited for `cmp …, VID_VGA` stragglers.

## 4. Availability and the Control Panel

`vid_probe_avail` treats `VID_EGA` like `VID_VGA`: the running adapter is
available by definition, and `VID_A_CGA` is set **unprobed** because mode 6 is
a standard BIOS mode on every EGA (the same reason a VGA gets it, §39.11.1).
It does **not** advertise a mode-12h path. So the Display page on an EGA
machine offers **EGA + CGA**, and `vid_switch` between them works through the
existing sequence (§39.11.2): `vid_setmode`'s EGA arm sets mode 10h, the CGA
arm mode 6.

`KERN_BIG`'s per-display context (§39.12–39.19) is **out of scope**.
`vid_dual_ok` already refuses everything but an exact Hercules+CGA pairing, so
an EGA changes nothing there. EGA+MDA as a period debugger rig
(`docs/plans/completed/DUAL-DISPLAY-VGA.md`) is a later question.

## 5. Testing

- **`make test VID_FORCE=4`** (build-time, mirrors `VIDEO=cga`'s
  `VID_FORCE=3`) — drives a real VGA / QEMU with `vid_tab`'s EGA row:
  `[vid_ch] = 350`, `wm_fit` / the chrome / the clip core all confine to it.
  It does not exercise the §39.1 EGA branch or mode 10h (the BIOS is still
  asked for 0012h, so the visible field is 480 tall) — the point is that
  nothing the kernel lays out reaches rows 350–479. Drive it with
  `tools/mouse.py --screen 640x350`.
- **`make test-full`** — the VGA byte-compare (`vga12.inc` output unchanged),
  both 1bpp adapters unaffected.
- **`make xt-ega`** — 86Box `ibmxt` with a real IBM EGA card and a 5154, the
  only way to exercise the §39.1 detection branch and mode 10h on a period
  BIOS. Interactive; no automation (docs/TESTING.md).
- **The 5160 itself** (Issue #136) — the field confirmation: full-height
  desktop, dock reachable, correct 16 colours.

## 6. Performance (target: 4.77 MHz 8088)

Nothing is added to a drawing path. EGA reuses the tuned planar bodies as they
are; the `[vid_mono]` dispatch already exists. A full EGA repaint is ~27 %
*less* work than VGA's (350 rows vs 480). The `SCREEN_H → [vid_ch]` edits swap
an immediate for a memory load in two transient XOR primitives (drag outline,
menu highlight) — per-gesture, not per-primitive, immaterial. The mode-set
clear shrinks (14,000 words vs 19,200). The `gfx_rowbase` table build in
`vid_apply` is unchanged (boot / adapter-switch only) and hits ~100 % of EGA's
rows. `VID_ROWTAB` is left at 348 on `kern_big` — bumping it to 350 to cover
EGA's last two rows is free (same 512-byte rung) but not done here; the miss
path on two rows is unmeasurable.

## 7. The Colour theme — done, in a commit of its own

The first draft of this plan deferred the Colour system theme (§76.12) on EGA
"via the Attribute Controller (`int 10h AX=1002h`)". **That reason was wrong.**
The Colour theme does no palette programming at all — it is six palette
*indices* (`CBLACK`, `CBLUE`, `CTEAL`, `CLGRAY`, `CDGRAY`, `CWHITE` =
0/1/3/7/8/15) that `thm_set` copies into an eight-byte live block, after which
every chrome site draws with the same `OSAPI_SET_COLOR` + `gfx_fill` /
`font_run` path that already renders Solitaire and Cyclone at 16 colours on an
EGA. The default 16-colour table is identical under EGA mode 10h and VGA
mode 12h.

The only thing that gated it was `thm_set`'s `cmp [vid_kind], VID_VGA` — an
**identity test standing in for a capability**, written while VGA was the only
colour adapter. `VID_EGA` is a colour adapter that is not a VGA, so the proxy
broke. Both that check and `cp_thm_colgrey`'s copy of it now accept `VID_EGA`
beside `VID_VGA` — the two four-plane kinds. (A first cut read
`[vid_mono] == 0` instead. On a VGA+Hercules extended desktop that byte names
the display *last drawn on* rather than the machine, and the Control Panel
reads the predicate from its own paint, so a panel dragged onto the Hercules
greyed Color on a VGA machine; §76.12.1 has the account.) No palette code, no
new draw logic, `THM_COLOR` and its `thm_tab` row already existed. Landed as a
separate commit from the geometry/detection work, with the §76.12 / §76.12.1
contract update. §76.12.4 (the extended desktop) is unaffected: an EGA is
never the primary of a *live* extended desktop — §39.13 pairs a colour card
with a mono one, and a switch to an EGA primary collapses the span
(`vid_disp_init`, §39.24) rather than sustaining it — so the primary there
is always the VGA and `[vid_kind]` answers as it always did.

## 7.1 Still deferred

- EGA in a **`KERN_BIG` dual-display** pair (EGA+MDA, EGA+Hercules). Note
  that `vid_dual_ok` refusing an EGA primary is not the whole story: because
  `vid_switch` moves `[vid_kind]`, the refusal has to be able to arrive with
  an Extend already running, and `vid_disp_init` collapses one when it does
  (§39.24). That is a teardown, not the pairing itself.
- An EGA driving a **monochrome display** (5151-class). It is detected —
  `int 10h AH=12h` returns `BH = 1` — and deliberately **not claimed**,
  because such a card has modes 7 and 0Fh and neither mode 10h nor mode 6,
  so every one of `vid_setmode`'s EGA arm, `vid_probe_avail`'s unprobed CGA
  bit and `vid_blank_kind`'s 3DAh would be addressing hardware that is not
  there. The right answer is a **mode 0Fh setup** — 640×350, planes 0 and 2,
  at `B000`, `[vid_mono]` 0 but only four colours — which is a second
  adapter setup rather than a flag on this one. Until then such a machine
  falls through to the equipment word and is driven as the mono card it is
  presenting as: untested, and honest about it (§39.1).
- **64 KB EGA** — mode 10h degrades to 640×350×4 (two planes); the four-plane
  writer's upper planes are ignored and the sixteen colours collapse toward
  black. Documented unsupported (§39.1), not a crash.

## 8. Open questions for the reporter

Being asked on Issue #136 in parallel; none blocks the change:

1. Monitor — IBM 5154 (350-line) or a multisync?
2. EGA RAM — 128 KB+ (needed for 16-colour mode 10h)? "16 of 64" implies yes.
3. After os8088 boots *today*, what does `int 10h AH=0Fh` report as the
   current mode? A stock IBM EGA BIOS has no mode 12h, yet the report says the
   640×480 path "operates" — knowing what the card actually accepted confirms
   mode 10h is the right target.
