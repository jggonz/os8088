# os8088 extended desktop on VGA + Hercules

**Research document, not a contract.** SPEC.md is the binding contract;
docs/DUAL-DISPLAY-PLAN.md is the study that produced §39.12–§39.19 and the
shipping Hercules+CGA extended desktop. This is the same question asked of a
**different pair of cards**, and the answer is short enough to put in the
first paragraph.

The ask, in the requester's words:

- Investigate **extended desktop support for VGA + Hercules**.
- **Do these use different memory locations?**
- **Can we use both at once?**
- If so, **make a plan**.

---

## 0. The verdict, up front

**Yes to both, and the second one is easier than it was for the pair that
already ships.**

1. **They use different memory, unambiguously.** A VGA in mode 12h decodes
   **A0000–AFFFF** and nothing else; a Hercules' page 0 is **B0000–B7FFF**.
   The two apertures do not touch, do not alias, and cannot be confused for
   one another. `VGA_SEG` is `0xA000` (`kernel/kernel.asm:34`) and
   `VID_HERC_SEG` is `0xB000` (`kernel/viddet.inc:51`) — the separation is
   already in the source, it has simply never been *used*.

   This is **cleaner than Hercules+CGA**, which is the pair the tree ships.
   Those two share the B segment: a Hercules' page 1 *is* B8000, which is
   where a CGA lives, so telling one card from two needed `vid_cga_alias`
   (§39.11.1) — a probe that has already been wrong once, in a way that threw
   away a perfectly good CGA. VGA+Hercules needs no such routine and can never
   have that class of bug.

2. **The machine can already run both at once, and the kernel already detects
   it.** `vid_probe_avail` runs *after* the mode is set precisely so a VGA has
   released B0000, and it then probes B0000 with `vid_memchk`. On a VGA machine
   with a Hercules in it, `[vid_avail]` comes out **0x07** today, on the
   shipping kernel, with no change at all.

3. **The whole extended-desktop machinery is built and is adapter-agnostic.**
   Steps 1–7 of docs/DUAL-DISPLAY-PLAN.md landed: the per-display context and
   swap (§39.12), the renderer's clip split (§39.2.1), second-card bring-up
   (§39.13), `GFXDISP` at the primitives (§39.14), the crossing cursor
   (§39.15), per-display `wm_fit`/drag/paint (§39.16), fullscreen (§39.17),
   fsx (§39.18) and the Control Panel's Extend/Right/Below with a `SYSTEM.CFG`
   key (§39.19). **None of it names an adapter.** What refuses VGA+Hercules is
   a single equality test.

4. **There is exactly one thing this pair has that Hercules+CGA never had, and
   it is the real work: MIXED COLOUR DEPTH.** Hercules and CGA are both 1bpp,
   one plane, both driven by the software renderer — so every display swap
   built so far has moved geometry between two displays that agree about *what
   kind of thing a framebuffer is*. VGA is four planes at 16 colours.
   `[vid_mono]`, `[vid_planes]` and `[vid_planes_w]` sit at
   `kernel/viddet.inc:172–175`, **outside** the eighteen-word run that
   `vid_ctx_act` copies (`vid_seg`:96 … `vid_chm8`:127), so a swap does not
   carry them. That, and what is sized in planes, is the change.

**Recommendation: build it, in the staging of §7.** It is a much smaller piece
of work than the original feature — an estimated **200–400 bytes** against that
one's 1,600 — because nine tenths of it is already in the tree. The binding
constraint is not difficulty but budget: see §6.

**Steps 0–4 are done** (§7.1–§7.4): the machine and its gate, the pairing
predicate, mixed colour depth across a primary swap, the raise cache sized for
the display it is on, and the text/reboot handoff naming the card. **The
budget question is settled** at a grant of up to 1KB (§6) and the work has
spent **162 bytes** of it. **The VGA runs mode 12h in colour rather than mode
11h in mono** (§4.1). What is left is **step 5, which is somebody's eyes** —
the dead zone and the aspect change across the seam, on 86Box or real iron —
and §8(8), a drag across the seam that hard-freezes a mixed-depth machine and
has its own handoff in docs/DUAL-DISPLAY-BUG2.md. The field machine can carry
this pair after a card swap (§8).

---

## 1. The hardware, checked rather than assumed

### 1.1 Memory

| | aperture | set by |
|---|---|---|
| VGA, mode 12h | **A0000–AFFFF** (64K) | Graphics Controller Miscellaneous register (GR06), memory-map field = 01b, which is what mode 12h programs |
| Hercules, page 0 | **B0000–B7FFF** (32K) | 3BFh bit 1 left **clear** by `vid_setmode`, deliberately |
| (CGA, for contrast) | B8000–BBFFF | — |

No overlap. Note the second row is *already* what `vid_setmode` does, and its
comment says why — "that page is at B8000, where a CGA in the same machine
lives". On a VGA+Hercules machine that reasoning is spare: the VGA is not at
B8000 either, so page 1 would in fact be free. Nothing needs it; the point is
that this pair has slack where the shipping pair has none.

### 1.2 Ports

| | range |
|---|---|
| VGA in a colour mode | 3C0–3CF, plus CRTC 3D4/3D5 and status 3DA |
| Hercules | 3B0–3BF |

No overlap — **conditional on the VGA's Miscellaneous Output register bit 0
being 1** (I/O Address Select = colour). Mode 12h sets it. A VGA in a *mono*
mode moves its CRTC to 3B4/3B5 and its status register to 3BA, straight on top
of the Hercules. That is not a problem for the desktop, which is always mode
12h; it is a problem for exactly two paths, and §4.4 is about them.

### 1.3 Is this a real configuration?

Yes, and more so than the pair the tree already supports. **EGA/VGA plus MDA
was *the* period dual-monitor arrangement** — it is what CodeView, Turbo
Debugger and MASM's own debuggers used, running the debugger on the mono
monitor beside the application on the colour one. Hercules+CGA is the rarer
build.

Which makes a comment in `kernel/vidsel.inc` worth correcting rather than
inheriting. `vid_blank`'s header says of VGA+Hercules:

> A VGA does not implement 3D8h at all, so a VGA-plus-Hercules machine keeps
> its VGA lit - accepted, that pairing being one nobody built

and SPEC.md §39.13 leans on that line to dismiss `VGA | HERC | CGA`. The
premise is false, and **so was this document's first reading of the hole.** It
said the hole was already closed because `vid_blank_kind` grew a real VGA arm
(SR01 bit 5, Screen Off) for §64's idle blanker. That routine did — and
**`vid_blank` never called it**: it carried its own copy of the darking, so the
switch path still wrote 3D8h at a VGA and the OUT was swallowed by the bus.

The copy had a second live defect with it, which is what a second
implementation of one operation costs. Its Hercules arm wrote **3B8h = 0**,
clearing bit 1 — the graphics bit — along with bit 3, so the card was not
blanked but put into MDA *text* mode with a 6845 still carrying 720×348
timings. `vid_blank_kind`'s own header records that exact defect, its fix
(`0x02`) and the measurement that found it; the fix was never brought back to
`vid_blank`. Both are corrected by deleting the copy — §7.2.

---

## 2. What the kernel already does on such a machine

Traced through the shipping source, with no changes:

| | answer today | right? |
|---|---|---|
| `vid_detect` | **VGA** — the AH=1Ah DCC probe answers before the equipment word is ever consulted, so a VGA+MDA machine boots VGA whatever SW1 says | ✓ |
| `vid_probe_avail` | sets the VGA bit; sets the CGA bit **unprobed** (mode 6 is a standard BIOS mode on any VGA); probes B0000 and finds the Hercules → **`[vid_avail]` = 0x07** | ✓ |
| Control Panel Display page | drawn (more than one bit set), offers VGA / Hercules / CGA | ✓, though "CGA" there is the VGA doing mode 6 |
| `vid_switch` to Hercules | works — `vid_avail_test` passes, `vid_equip` sets 40:10 to mono, `vid_setmode` programs the 6845 directly | ✓ |
| `vid_blank_kind` / `vid_unblank_kind` | real arms for all three adapters | ✓ |
| **`vid_dual_ok`** | **CF=1, refused** — it tests `[vid_avail] == VID_A_HERC \| VID_A_CGA` **exactly**, and 0x07 is not 0x06 | **this is the gate** |
| `vid_disp_init` | never reached | — |

So the feature is one predicate away from being *attempted*, and the mixed-depth
work away from being *correct*.

---

## 3. The predicate is wrong, and fixing it is a bug fix

SPEC.md §39.13 gives three cases. Two are right and one is not:

- **`VGA | CGA` is one card.** Correct, and the reason is exact: the CGA bit is
  set on every VGA without probing anything. A bit count would report two
  displays on the commonest machine in the tree and program the VGA twice.
- **One bit** is the ordinary machine. Correct.
- **`VGA | HERC | CGA`** — the spec says *"the bitmap cannot say whether the CGA
  bit is that VGA's mode 6 or a third card"*, and concludes the whole value must
  be refused. **The premise is true and the conclusion does not follow.**

What the bitmap *can* say is the part that matters. **The HERC bit is only ever
set by evidence**: either `vid_memchk` found memory at B0000 that the running
VGA (in mode 12h, decoding A0000 only) demonstrably is not, or the machine is
running on it. The CGA bit is the synthetic one. So on `VGA | HERC | CGA` the
machine provably has **a colour card and a mono card**, and the only thing
unknown is whether there is *also* a real CGA — which changes nothing, because
that third card would simply go unused.

The rule expressed as what it means:

```
two displays  =  the HERC bit is set  AND  a colour bit is set
the colour partner  =  VGA if the VGA bit is set, else CGA
```

which yields, on every value:

| `[vid_avail]` | machine | displays |
|---|---|---|
| `HERC \| CGA` (0x06) | Hercules + real CGA | 2 — **unchanged, ships today** |
| `VGA \| HERC \| CGA` (0x07) | VGA + Hercules | **2 — new** |
| `VGA \| CGA` (0x05) | one VGA | 1 — unchanged |
| one bit | one card | 1 — unchanged |

The pairing that must never be made is **VGA with CGA**, because on a VGA
machine those are one card — and that falls out of "the colour partner is the
VGA if there is one" rather than needing a test. The refusal stops being an
equality against a magic number and starts being a sentence about cards.

**Accepted limitation, stated rather than hidden:** VGA + Hercules + a real CGA
is indistinguishable from VGA + Hercules, and gets VGA + Hercules. The third
card is never programmed. That is the same class of thing §39.13 already
accepts, and the alternative — probing B8000 for a card distinct from the VGA's
own mode 6 — is `vid_cga_alias`'s problem again, on a machine nobody has.

---

## 4. What actually has to be built

### 4.1 Colour or mono: mode 12h, not mode 11h

The obvious way to make mixed depth disappear is to run the VGA in **mode 11h**
— 640×480, two colours, one plane — so that both displays are 1bpp and the pair
becomes exactly the Hercules+CGA case the tree already ships. It is the right
instinct and it is the wrong trade, for three reasons in increasing order of
weight.

**It would cost every VGA user their colour desktop.** The primary is the
display the machine spends its life on. Minesweeper's sixteen colours, Paint's
palette, Solitaire's red pips, Missile Command's explosion ramp and ModPlug's
green LCD all reduce to black, white and a 50% dither (§39.4) — on a machine
with a perfectly good colour card in it — in exchange for a second monitor. A
feature that makes the *unextended* machine worse is a feature that will be
turned off.

**Mode 11h is not the linear 1bpp framebuffer it looks like.** It is the same
planar hardware with one plane displayed: a CPU read returns latched data
through Read Map Select and a write goes through the Graphics Controller's write
mode, bit mask and map mask. `softgfx.inc`'s read-modify-write would work only
while the GC is left in exactly the state the BIOS mode set happened to leave it
(write mode 0, map mask 01, bit mask FF, set/reset off) — a standing hardware
invariant that nothing in the kernel maintains, because on mono adapters today
there *is* no GC. That is a "works until something writes a Graphics Controller
register" bug, and it is a worse class of thing than the work it avoids.

**And the colour costs almost nothing, because reduction is already the
kernel's job.** §39.4's palette reduction happens in `gfx_ink`/`sw_ink`, which
sit **below** `GFXDISP` — so an app that draws colour 12 into a window on the
Hercules gets it dithered by the renderer, automatically, with no knowledge that
its window is on a mono display. That is the property that makes mixed depth a
kernel change of ~90 bytes rather than an app-compatibility event: **no package
needs a line, and every package's pixels are correct on both displays.**

**What it does cost, stated plainly, is one thing.** `osapi_video` answers
`DL` = `[vid_kind]` and `DH` = planes, and outside a drawing primitive the live
block is the primary's — so an app whose window sits on the *secondary* is told
"VGA, 4 planes" while its window is being drawn 1bpp. Sixteen call sites across
ten packages read that. The **pixels stay correct** (the kernel reduces); what
can be wrong is a *performance strategy* chosen from it — Missile Command picks
its explosion ramp on `cmp dh, 1` (§48.22.1), so on the Hercules it would draw
the fine ramp: more drawing calls, same output. Nothing is broken, and a later
step could answer `OSAPI_VIDEO` for `wm_disp_of`'s display instead of the
primary. That is a refinement, not a prerequisite, and it is exactly what the
1KB budget has room for.

### 4.2 Mixed depth — the one genuinely new thing

`vid_apply` derives four pieces of renderer state from `[vid_kind]`:

```
vid_mono      db   1 on Hercules/CGA, 0 on VGA   ; routes EVERY primitive
vid_planes    db   1 or 4
vid_planes_w  dw   the same, where a word is wanted
vid_kind      db   already banked in the record as VID_CTX_KIND
```

All four are at `viddet.inc:171–175`, **outside** the asserted contiguous
eighteen-word run `vid_ctx_act` copies. A swap onto a Hercules display on a
VGA-primary machine would therefore leave `[vid_mono]` = 0 and the *planar*
renderer pointed at B0000.

**The placement is already right, which is the good news.** `GFXDISP` sits
**below** `GFXCLIP` and **above** the `cmp byte [vid_mono], 0` dispatch at all
four rect entries (`vga12.inc:2076–2080`, `2191–2195`, `2335–2339`,
`2680–2682`). So if the swap carries those bytes, the existing dispatch picks
the right renderer with no further change anywhere — the same property that let
one `GFXDISP` cover ten public entries and the whole of blitting.

Two ways to carry them:

- **(a) extend the copied run.** Rejected: they are `db`/`db`/`dw` interleaved
  with `vid_kind`, they are *derived* rather than `vid_tab` columns, and the
  run's contiguity is asserted at assembly time in two places.
- **(b) derive them in `vid_ctx_act` from `VID_CTX_KIND`**, which the record
  already banks and which `vid_ctx_capture` already maintains. ~10
  instructions and one branch — the same derivation `vid_apply` does, in the
  one other place that changes which display is live. **Recommended.**

`[vid_rseg]`, `[vid_rpara]` and `[vid_rend]` — the software renderer's target
and its loop bounds — are *inside* the run already and need nothing.

### 4.3 What is sized in planes

Mixed depth makes "how many bytes is this rectangle" a per-display question for
the first time. Two consumers:

- **The window raise cache (`wm_su_*`, §11.96)** claims
  `w × h × [vid_planes_w]` (`wm.inc:6472, 6535, 7126, 7251`). A cache taken on
  the VGA is **four times** the bytes of the same rect on the Hercules, and
  `wm_su_ck` compares **rects** — so a window whose rect does not change while
  its display does would restore a quarter of a picture, or read past a claim.
  §39.14.8.1 already met this shape when `vid_switch` moved the primary and
  answered it with `wm_su_drop_all`; here the answer is to record the display
  (or the plane count) in the cache and drop on mismatch, since dropping *all*
  of them on every seam crossing would defeat the cache the feature exists for.
- **The menu save-under (`menu.inc:1850`)** is the same arithmetic and is
  **safe as it stands**, because DUAL-DISPLAY-PLAN §3.1 puts the chrome — and
  therefore every pull-down — on the primary alone.

#### 4.3.1 …and the DRAG cache crosses displays by construction

Step 3 sized the raise cache for the display it is **on**, which is the right
answer for a window that is banked and put back **where it was**. §11.96.12's
**drag** cache is the case that moves: `wm_dc_take` banks the content at the
old position and then **relabels the header by the drag's delta** so the
replay lands at the new one. On a mixed-depth machine those two positions can
be on cards of different depth, and then the buffer means something else
where it lands — `gfx_save` laid it out as `w × h / 8` bytes of one plane on
the Hercules, and the restore writes four on the VGA.

**Reported from the field on 86Box**, VGA primary with a Hercules secondary:
drag a Disk window onto the Hercules (correct), then drag it back onto the
VGA and its content arrives as **magenta and cyan noise inside a correct
frame** — which is exactly what 1bpp bytes look like rendered as mode 12h's
four planes. The two drags before it are correct for reasons that are
themselves informative: the straddling one is refused by `wm_su_take`'s
`vid_span_one`, and the one *onto* the Hercules has a straddling **source**
and so is refused the same way. **Only the last drag has a source wholly on
one card and a destination wholly on the other**, which is the one case the
cache accepts and cannot serve.

**The refusal belongs beside the byte-phase one**, and is the same argument
one axis over. `wm_dc_take` already declines a horizontal drag whose delta is
not a multiple of 8, because `gfx_save` lays the buffer out from `x1` rounded
down to a framebuffer byte and the pixels can only be replayed where they sit
at the same offset inside their byte. Depth is that argument about *planes*:
compare `vid_disp_planes` at the old origin with the new one and refuse when
they differ. It costs **one full repaint on the one drag in a session that
crosses a depth change**, and **Hercules+CGA can never reach it** — both are
1bpp, so the two answers are always equal and the test is a compare.

### 4.4 Bringing a VGA up as the *secondary*

`vid_disp_init`'s `.extend` path makes the secondary momentarily live and calls
`vid_setmode`. Two cases, and they are not equally safe:

- **VGA primary, Hercules secondary** — the default, since `vid_detect` answers
  VGA on any VGA machine. `vid_setmode`'s Hercules path is direct 6845
  programming at 3BF/3B8/3B4 and touches no VGA register at all. **Clean, and
  this is the case to build first.**
- **Hercules primary, VGA secondary** — reachable, because the Display page can
  make the Hercules primary. `vid_setmode` then issues `int 10h AX=0012h` with
  the equipment word saying mono. This is the mirror of the problem
  `vid_cga_equip` exists for. A VGA BIOS's mode set is *not* equipment-driven
  the way an XT ROM's is — it keeps its own state — so it ought to be fine, and
  "ought to be fine about a video ROM" is what SPEC.md §39.6 is a monument to.
  **Unverified. Stage it separately (§7, step 4).**

And one hazard that is genuinely new, because it is about a mode this pair can
reach and the shipping pair cannot:

- **`vid_text` (CMD_REBOOT) with the Hercules primary.** It takes the Hercules
  branch and issues `int 10h AX=0007h` — mode 7, mono text. On a VGA+MDA
  machine int 10h belongs to the **VGA**, and mode 7 puts the VGA's aperture at
  **B0000** and its CRTC at **3B4** — directly on top of the real Hercules, both
  in memory and in I/O. Two cards driving one address space is exactly what
  §39.6 leaves 3BFh bit 1 clear to avoid. The reboot path has to name the card,
  not the mode.

### 4.5 What needs nothing

Worth stating, because it is most of the feature:

- The per-display context, the swap, `vid_disp_of`/`vid_disp_find`,
  `vid_pt_local`/`vid_pt_clamp`, `vid_span_one`, `vid_desk_union`.
- `GFXDISP`/`GFXDENTER`/`GFXDORG` and every primitive under them.
- The crossing cursor and `mou_clamp` (§39.15).
- `wm_fit`, `ui_drag_dead`, `wm_disp_of`, both fullscreens, the fsx bracket.
- The Control Panel's Extend / Right / Below row and the three-byte `VM` key —
  a VGA+Hercules machine needs **no new control**, because the page already
  lists the adapters this machine has and the desktop row is adapter-blind.
- `vid_blank_kind` / `vid_unblank_kind`, which already have a VGA arm.

---

## 5. Geometry, and two consequences to accept

640×480 beside 720×348, laid out by §39.19.2's rule that the primary sits at the
virtual origin and the secondary's top row aligns with the *desktop band* rather
than the screen (§39.19.3):

```
VGA primary, Right:              Hercules primary, Right:
  d0 VGA  (0,0)   640x480          d0 HERC (0,0)   720x348
  d1 HERC (640,20) 720x348         d1 VGA  (720,20) 640x480
  desktop 1360x480                 desktop 1360x500
  dead zone 720x112 (below d1)     dead zone 720x152 (below d0)
```

**The dead zone is larger than the shipping pair's**, and it changes sides
depending on which card is primary — because for the first time the *secondary*
can be the taller display. Nothing in the renderer cares (a fragment landing in
no display is drawn by nobody, with no test for it anywhere), and `mou_clamp` is
written against the union rather than a rectangle, so both behaviours fall out.
It is a thing to look at, not a thing to code.

**Pixel aspect changes across the seam, and that is new.** VGA mode 12h is
roughly square; Hercules 720×348 is about 1:1.55; CGA 640×200 is 1:2.4. The
shipping pair are both "tall pixel" adapters, so a window dragged across that
seam changes size but keeps its proportions roughly. Dragged from a VGA onto a
Hercules it will visibly change *shape*. This is inherent — two cards, two
rasters — and the only alternative is scaling, which this machine cannot afford.
Accept and document, as the dead zone was.

---

## 6. What it costs, and the constraint that actually binds

Where this started — the **branch base**, `7c5e1d1`, built and read rather
than quoted (`make`, `tools/kernsize.py`), `kern_big`:

```
kernsize[big]: rungs      image 55,296 (220 left)   cold 35,328 (505 left)
kernsize[big]: footprint  KERN_SIZE 104,448 of KERN_BUDGET 104,960 -> 512 spare (1 step)
kernsize[big]: segment    .text+.bss 55,076 of KERN_CODE_MAX 65,536 -> 10,460 left
```

**That 512 spare is where the tree already was and is not this work's doing** —
worth stating because `make` prints its deltas against docs/KERNEL-MEMORY.md's
last *blessed* row, which for `kern_big` is older than the branch point and so
reports a standing `[+1,536]` that belongs to whatever landed in between. A
figure a build prints is not automatically a figure about your change.

Estimate, per §4 — **and the measurement, now that steps 0–3 are built.**
Measured by building the branch base (`7c5e1d1`, which is also where
docs/KERNEL-MEMORY.md's kern_big row was last blessed) and reading
`kernsize.py` at each commit, so these are the branch's own deltas and not the
stale-baseline figures `make` prints:

| | est. | **actual (kern_big `.text`)** |
|---|---|---|
| step 0 — the machine and the gates (§7.1) | 0 | **+0** (harness only) |
| `vid_dual_ok` + `vid_disp_other` (§3) | ~40 B | **+40** } steps 1+2 |
| mono/planes/planes_w on the swap (§4.2) | ~30 B | **+22** } together |
| raise cache tagged by display (§4.3) | ~60 B | **+0** (see below) |
| `vid_text` / secondary-VGA mode-set ordering (§4.4) | ~50 B | not built |
| Control Panel, `SYSTEM.CFG` | **0** | **+0** |
| **total so far** | ~200–400 B | **+62 B** |

**No rung moved and the footprint did not change**: `KERN_SIZE` is 104,448
before the branch and 104,448 now, the image rung's slack going 220 → 158
bytes inside the same 512-byte step. Per docs/KERNEL-MEMORY.md's accounting
rule that is *not* "free" — 62 bytes of the next feature's slack are spent —
but the machine's RAM has not moved. The remaining §4.4 work has 158 bytes of
rung before it costs a step, and 512 of budget after that.

**`kern_small` is +3 bytes and that is the whole of it**, against the "+0
throughout, not negotiable" below. It was **+17**, and the 14-byte difference
is worth recording because it is a shape rather than an oversight: step 3's
`wm_su_pw` is a real word on a machine with two displays of different depth,
and on a single-display build it can only ever hold what `[vid_planes_w]`
holds — so the small build was assembling a word, a store and four
bank/restore instructions to feed itself a number it already had. Selecting
the **name** at assembly time (`%define WM_SU_PW`) rather than keeping the
word and filling it takes those 14 back, and leaves the shipped `kern_big`
**byte-identical** — same `md5sum` on `build/kernel.bin`, which is a stronger
statement than any gate passing.

The residual 3 are steps 1+2's, and they are `vid_depth_set`: on `kern_small`
it has exactly one caller, so it is a `call`/`ret` where the stores used to be
inline. They buy the property step 2 exists for — **one place decides depth** —
and inlining it again under an `%ifdef` would put a second copy of that
decision in the source to save three bytes on a build with 1,536 spare. Left
as it is, deliberately.

**`KERN_BUDGET` has one 512-byte step left, and the answer is a grant of up to
1KB** — asked for and given, so this is settled rather than pending. 200–400
fits inside it with room for §4.1's `OSAPI_VIDEO` refinement, for §4.3's raise
cache if it wants more than the estimate, and for the bug fixing that a change
touching the renderer's dispatch will want. What the grant does *not* license is
spending it because it is there: the accounting rule in docs/KERNEL-MEMORY.md
still applies, and whatever the work does not use is handed back.

`KERN_CODE_MAX` has 10,398 bytes free and is not the constraint. `kern_small` is
single-display by construction and should measure **+0** throughout — that
property is not negotiable and is the cheapest regression test in the plan.

**It is +3, and it caught something.** The rule earned its keep at step 3: the
small build was 17 bytes up, and reading *why* found a word it was filling with
a number it already had (above). Two ways of using a gate like this, and only
one of them is any good — "is the number small enough to accept" learns
nothing, and "what is the number made of" is what turned 17 into 3 and left
`kern_big` byte-identical. Check it with `make KERN_SMALL=1` and read the
`.text` figure against the branch base's **46,314**.

---

## 7. Staging

Each step testable on its own, with byte identity on the three single-card
adapters as the standing gate — the `REDRAWFULL=1` discipline (§12.9/§30.3) that
every step of the original plan was held to.

| # | step | gate |
|---|---|---|
| 0 | ~~**MartyPC: a VGA + Hercules machine**, and answer the aperture question below~~ **DONE** — §7.1 | `tests/dualcheck.py --machine os8088_xt_vga_herc` passes, the Hercules+CGA machine still passes, and an injected alias **fails** |
| 1 | ~~Correct the pairing predicate (§3): `vid_dual_ok`, `vid_disp_init`'s partner. Also correct `vid_blank` (§1.3)~~ **DONE** — §7.2 | `tests/dispmode.py --machine os8088_xt_vga_herc` places both displays both ways round and both primaries; **0 differing pixels** on all three single-card adapters |
| 2 | ~~**Mixed depth** (§4.2): carry mono/planes/planes_w on the swap~~ **DONE, and it had to land WITH step 1** — §7.2 | the same run: no memory corruption on a primary swap, byte identity single-card |
| 3 | ~~Raise cache per display (§4.3)~~ **DONE** — §7.3 | `tests/dispsave.py --machine os8088_xt_vga_herc` reads `wm_su_pw = 1` against `vid_planes_w = 4`; the Hercules+CGA machine reads 1 and 1 and passes unchanged |
| 4 | ~~Hercules primary with a VGA secondary; `vid_text`/CMD_REBOOT naming the card (§4.4)~~ **DONE** — §7.4 | `tests/disptext.py` reads the VGA at CRTC **0x3D4** after the handoff where the pre-fix kernel reads **0x3B4**, both primaries, and `--entry vid_text` shows the fsx path leaving the other card alone |
| 5 | Look at it (§5) — dead zone, aspect change across the seam | somebody's eyes, on 86Box or real iron |

**Step 0 is not a formality and the reason is patch 02.** Upstream MartyPC maps
a Hercules-subtype MDA at B0000 *and* B8000 unconditionally, in the constructor,
before any guest has written 3BFh — and `Bus::register_map` resolves overlap by
last-writer-wins. One card silently vanished into the other, and **the obvious
test passed on the broken machine**. The identical question has to be asked of
MartyPC's VGA: does it map its aperture from GR06, or claim B0000/B8000 at
construction? If it claims them, it will swallow the Hercules and every dual
test in this plan is a convincing false pass. The test that discriminates is the
one that asks the **rasters** — a write to one card's memory must change *that
card's* rendered output and not the other's — and `tests/dualcheck.py` already
does exactly that and only needs a VGA arm.

### 7.1 Step 0, as built

`os8088_xt_vga_herc` in `tools/martypc/configs/os8088_machines.toml`, and
`tests/dualcheck.py` generalised to cover both two-card pairs. **No MartyPC
patch was needed, which is the opposite of what the Hercules+CGA pair found.**

**The aperture question answered itself out of two constructors.**
`vga/mmio.rs`'s `get_mapping()` claims **A0000** and **B8000**; `mda/mmio.rs`'s,
since `patches/02`, claims **B0000** alone. Disjoint — so `Bus::register_map`'s
last-writer-wins has nothing to resolve and the order of the two
`[[machine.video]]` blocks decides nothing, where for `os8088_5150_both` it
decided which card silently vanished.

**One gate, not two.** Everything that differs between the pairs is a row in
`CARDS` — the aperture, the fill that lights it, how the card is brought up —
for `gfxbench`'s reason: two sources measuring "the same" thing drift, silently.
Each card is now driven through **the aperture os8088 itself uses**, which the
Hercules+CGA version already did by accident and this one had to be made to do
on purpose.

**Two things about the instrument had to be found the hard way, and both were
silent.**

*A VGA programs itself, and parking the CPU stops it.* The gate's whole design
is to park the guest in `jmp $` and drive the cards from the debugger, which is
right for a Hercules and a CGA — no BIOS here touches either. A VGA has an
option ROM, and parking before POST has run leaves the card at **0 frames for
ever**. `advance frames=` on a counter that never moves then spins to the debug
server's own 300-second timeout, so the symptom is a gate that appears to hang,
five minutes away from the cause, on a machine where both cards are present and
one of them is fine. `warm_up` runs POST until every self-programming card is
rasterising — **two** consecutive advances that each move the counter, because
one can be caught inside the ROM's own mode set — and parks after.

*Mode 3 is the wrong surface and it fails in a way that looks like a real
defect.* The card POSTs into mode 3, whose aperture is B8000 — not the A0000
os8088 draws mode 12h through — and text mode is odd/even planed: characters in
plane 0, attributes in plane 1, and **the debug server's read path returns
plane 0 alone**. `11 22 33 44` written there reads back `11 00 33 00`, which
failed check 2 on a machine with nothing wrong with it. The writes are fine — a
DB/07 fill lights 288,000 pixels — so it is the *read* that cannot see half of
what is there. The fix is better than a workaround: five bytes of guest code
(`mov ax,0x0012` / `int 0x10`) run as `park_cpu`'s **prologue**, so the card's
own ROM sets the mode os8088 actually uses and the CPU falls into the same
`jmp $` as ever.

Measured, with the Hercules+CGA machine re-run unchanged in the same session:

| | VGA + Hercules | Hercules + CGA |
|---|---|---|
| cards | `vga` 0, `mda` 1 | `cga` 0, `mda` 1 |
| apertures tested | **A0000** / B0000 | B8000 / B0000 |
| colour card filled | **307,200 lit** (= 640×480 exactly), mono card **+0** | 102,024 (+85,184), mono **+0** |
| mono card filled | **244,992 lit**, colour card **+0** | 244,992, colour **+0** |
| geometry | 640×480 vs 720×350 | 640×200 vs 720×350 |
| frame clocks | 30 : 24 | 30 : 24 |
| verdict | **PASS** | **PASS** |

**And it is verified to fail.** Injecting the fault it exists to catch — the
VGA's aperture moved inside the Hercules' — is caught at check 3: *"writing the
VGA card's memory did not change the VGA card's raster"*. That run is also a
live demonstration of the docstring's own warning, because **check 2 passed on
the aliased configuration**, which is exactly why it decides nothing.

### 7.2 Steps 1 and 2, as built — and why they could not be separated

`vid_dual_ok` rewritten, `vid_disp_other` added, `vid_blank` reduced to its
test, and `vid_depth_set` carrying the renderer's depth across a display swap.
**Cost: `.text` +37, `.cold` +4, no rung crossed** — 194 bytes left in the image
rung — against the 1KB grant.

**Step 1 alone corrupts kernel memory, which is why step 2 is in the same
commit.** The plan had them separate and the plan was wrong: the moment the
predicate admits a VGA+Hercules machine, the first primary swap splatters
`0xFF` across kernel `.text`. §4.2 predicted the mechanism and the measurement
is sharper than the prediction — `[vid_rseg]` **is** in the eighteen-word run,
so activating the VGA display carries its rseg (which `vid_apply` sets to **0**,
nothing on a VGA routing through the software renderer) while `[vid_mono]`
still says 1 from the Hercules primary. A white fill then writes 0xFF through
**segment zero**. `[vid_ndisp]` reading **170** was a casualty of that, not a
bug of its own — 0xAA is simply what a white byte looks like when it lands on a
display count.

**The diagnosis took a wrong turn worth recording.** 170 and 85 are 0xAA and
0x55, which read as a deliberate `0x55AA` word and sent the investigation
looking for a boot signature; and the one new call on that path is
`int 10h AX=0012h` — §4.4's flagged hazard — so the VGA BIOS was the obvious
suspect. It was innocent: driven from a parked stub with the pattern in place
and ES pointed at the kernel segment, the mode set left `0x0176F` **intact**.
What settled it was widening the window from 2 bytes to 128 and finding a
*bulk* 0xFF fill rather than a word. **A two-byte reading of a wide fill looks
exactly like a deliberate store.**

**What `vid_blank` cost by being a copy** is in §1.3: two live defects, one of
them a Hercules "blank" that was really a mode change, both fixed by delegating
to `vid_blank_kind`.

Measured on `os8088_xt_vga_herc`, driven through the Control Panel end to end:

| | |
|---|---|
| at boot | `kind 0 avail 0x7 ndisp 1 dmode 0`, desktop 640×480 = chrome, Hercules **stopped scanning** |
| `Right` | `ndisp 2`, d1 = **kind 1 at (640,20) 720×348 seg b000**, desktop **1360×480**, chrome 640×480 |
| `Below` | d1 at **(0,480)**, desktop 720×828, chrome unchanged |
| primary → Hercules | `kind 1 ndisp 2`, d0 = Hercules at (0,0), d1 = **VGA at (720,20) seg a000**, chrome **720×348** — `vid_disp_other`'s other arm, the one that must answer VGA rather than CGA |
| primary → back | `kind 0`, d1 at (640,20) |
| reboot | comes up extended, from the `VM` key, with nobody clicking |

and **0 differing pixels** on `os8088_5150_cga_gla`, `os8088_5150_herc_gla` and
`os8088_xt_vga` against the pre-change build — which matters more than usual
here, because `vid_depth_set` is a refactor of `vid_apply` and so is on every
build's path, `kern_small` included. `os8088_5150_both_gla` re-runs identical
to the reference, including the pre-existing failure in §8(7).

**One harness fix went with it.** `os88mouse._edge` sent its packet once and
then polled 60 times — and its own docstring says the UART *drops* a packet
sent while the previous one is in flight, which no amount of polling recovers.
It re-sends every 20 polls now. That is safe because a Microsoft packet carries
the button's **level** and not an edge, so a duplicate arriving after the first
was decoded says what the guest already believes; the loop still requires the
guest's published `mouse_btn` to agree, so a re-send cannot turn a stuck button
into a pass. Without it the last three steps of `dispmode` were unreachable on
this machine.

### 7.3 Step 3, as built

`wm_su_pw` — the plane count of the display a raise cache is **on** — set by
`wm_su_flay` and read by the four sites that used `[vid_planes_w]`
(`wm_su_bytes`, `wm_su_scrset`, `wm_su_edge`, `wm_su_merge`).
**Cost: `.text` +36, no rung crossed.** Running total for the whole feature:
**`.text` +73, `.cold` +4** of the 1KB grant.

**It is a heap overrun and not a wrong picture, which is why it was worth
finding.** `gfx_save` takes `GFXDENTER`, so it writes with the planes of the
display the rect is **on**; `wm_su_flay` sized the claim from `[vid_planes_w]`,
which is whichever display is **current** — the primary, because every hook
restores it. On a VGA primary with a window on the Hercules the claim is four
times too big: wasteful, and it looks perfect. With the **mono card primary and
a window on the VGA** the claim is sized ×1 and `gfx_save` writes ×4.

**It is derived, not stored, and that is the design decision.** §4.3 offered
"record the display (or the plane count) in the cache"; the claim's header
already carries the rect the pixels were taken over, and the display is a
function of that rect, so a second field would be a second opinion that can go
stale. The header's own comment makes the argument for the rect — *"it travels
with the pixels it describes and cannot get out of step with them"* — and
`vid_disp_planes` is that sentence one field further.

**One choke point, because there is exactly one.** `wm_su_flay` is where both
the take (`wm_su_take`) and the check (`wm_su_ck`) establish the content rect,
so setting it there covers all four consumers and there is no path that sizes a
buffer without it.

**The safety property is stronger than the test.** On any machine whose
displays agree about depth — every single-card machine and the whole
Hercules+CGA pairing — `[wm_su_pw]` holds exactly what `[vid_planes_w]` holds,
by construction. Measured: **0 differing pixels** on CGA, Hercules and VGA, and
`dispsave` on `os8088_5150_both_gla` unchanged.

| | VGA + Hercules | Hercules + CGA |
|---|---|---|
| secondary | mda at x=640 | mda at x=640 |
| cache taken | `wm_su_segs[0] = 0x9E00` | `0x9E80` |
| **`wm_su_pw` / `vid_planes_w`** | **1 / 4 — they differ** | 1 / 1 |
| pixels after the raise | 63 differ of 64,000 | 85 of 49,600 |

That middle row is the whole gate. The pixel counts pass either way, because
the direction reachable in a scripted session is the *safe* one — so a test
that only looked at pixels would have passed a kernel with the bug in it.

### 7.4 Step 4, as built

`vid_text` names the card; `vid_mono_text` programs the real Hercules when
`int 10h` would land on a VGA; `vid_reboot` gives the card that *will* print
a mode of its own. SPEC.md §39.20/§39.20.1 is the contract, and
`tests/disptext.py` is the gate. **Cost: `.text` +100 on both builds, no rung
crossed** — the image rung has **58 bytes** left, so the next addition
anywhere in `.text` buys a 512-byte step and that step is the last one under
`KERN_BUDGET`.

**It is a PRE-EXISTING defect and not this feature's**, which is the first
thing to say because it is being fixed on a branch about extended desktops.
Reaching it needs no second display: a VGA and a Hercules in one machine,
§31.10's Display page used to make the Hercules primary, then a reboot — all
of which the *shipped* kernel does today, because `vid_dual_ok` gates the
desktop and never `vid_switch`. Both kernel builds carry the fix for that
reason, and `kern_small` is +100 with it. That is not §6's "+0" rule being
relaxed: the rule is about what the *extended desktop* costs a machine that
cannot have one, and this is a bug that machine already has.

**§4.4's other half needed no code at all.** "Hercules primary, VGA
secondary" was flagged unverified on the reasoning that a VGA BIOS's mode set
*ought* to be fine with the equipment word saying mono — and it is: §7.2's
table already shows that arrangement coming up correctly, with the VGA at
(720,20) in `seg a000`, and every run of `disptext` below re-establishes it
as its setup. The hazard was real and it was entirely in the **text** path.

**The measurement is an A/B, because a gate that only passes proves nothing.**
The same script against the pre-fix kernel (`--entry vid_text`, which is what
that kernel has) reports the bug in the terms it was predicted in:

| | pre-fix | fixed |
|---|---|---|
| `40:49` — the mode the VGA BIOS set | **0x07, mono text** | 0x03 |
| `40:63` — the CRTC that mode uses | **0x3B4, the Hercules' own** | 0x3D4 |
| the mono card's raster across the call | 153,881 → **52,331 lit** | 153,881 → 0 |
| B0000 after it | 2000/2000 blank — *from whichever card answered* | 2000/2000 blank |

That third row is the conflict caught in the act rather than inferred: **3B8h
is a port the VGA implements in mono modes and the Hercules decodes always**,
so the VGA BIOS's mode 7 setup reprogrammed a card it does not know exists.

**Three runs, all passing on the fixed kernel:**

| run | what it establishes |
|---|---|
| `disptext.py` | Hercules primary: VGA at CRTC 0x3D4 and a 720×400 text raster, mono card holding a cleared 80×25 page |
| `disptext.py --primary vga` | the other direction is untouched — mode 3, CRTC 0x3D4 |
| `disptext.py --entry vid_text` | **the fsx path leaves the VGA at 640×480 in mode 0x12**, which is the property §39.18 needs: inside a bracket that card has been darked and a mode set would light it |

**Two things about the instrument are worth keeping.** `dualcheck`'s raster
discriminator is the *right* question for an aperture and cannot be used
here — MartyPC does not rasterise MDA text, so the mono card reads 0 lit
whatever is in its memory, and a VGA in mode 3 has a blinking cursor, so its
raster moves ~16 pixels between any two samples on its own. Both were
measured before the assertion was rewritten to read the VGA BIOS's own record
instead. And **calling the thing under test needed a stub**: it sits three
instructions ahead of an `int 19h`, so the state to be measured is destroyed
by the call that produces it — the machine is driven to the arrangement, then
parked, and the parked CPU pointed at `cli / mov ax,cs / mov ds,ax / call
vid_reboot / jmp $` written over `menu_bcell`. That is `park_cpu`'s prologue
idiom one segment along, in `KERNEL_SEG` because the call is near.

**What is NOT fixed, deliberately: the secondary is left showing a stale
desktop.** With the VGA primary, `vid_reboot` leaves the Hercules at 122,496
lit — the extended desktop, frozen, across the reboot. It is symmetric with
what the shipping Hercules+CGA pairing already does, it is cosmetic, and
darking it would change behaviour on a pairing this branch is not about.
Noted rather than done.

**And one thing here is the 5150's, for the same reason as everything else in
§8: MartyPC does not rasterise MDA text.** The 6845 values written, the page
cleared and the 3B8h byte are all verified — but *that those values produce a
readable 80×25 screen on a real Hercules* is not, and cannot be here. It is
IBM's own mode 7 table with one changed register, so the risk is low and the
statement is still unverified. The same limit is what makes §45.13's Tracker
text screen untestable on that card in this container.

---

## 8. What is unverified, and the one that outranks the rest

1. **A field machine CAN test this, at the price of moving cards.** This
   document first said no machine could, reasoning from docs/FIELD-MACHINES.md:
   the calibration 5150 is Hercules+CGA permanently, and the only VGA in the
   register is the Packard Bell 286's onboard Paradise PVGA1A with no Hercules
   beside it. **The register describes the machines as they are configured, not
   as they can be** — the hardware owner has confirmed a VGA and a Hercules can
   be put in one box, since there is only one Hercules card to go round. So the
   three defects an emulator cannot show (a visible redraw, a double-draw flash,
   input overrun) *are* observable for this pairing.

   It is a card swap rather than a disk in an envelope, so it is not the
   every-commit instrument the 5150 is for the shipping pair. Emulators carry
   the development (§7's gates are all mechanical for that reason) and the field
   run is the confirmation at the end — which is where docs/FIELD-MACHINES.md
   puts a field run anyway.
2. ~~**Whether MartyPC's VGA and MDA genuinely coexist**~~ — **answered: they
   do** (§7.1), and the gate that says so is
   `tests/dualcheck.py --machine os8088_xt_vga_herc`.
3. **Whether a VGA BIOS will set mode 12h with the equipment word saying mono**
   (§4.4). Reasoned, not tested.
4. **Whether `int 10h` mode 7 on a VGA+MDA machine really lands on the VGA**
   (§4.4's reboot hazard). It follows from the VGA owning the vector, and it has
   not been observed.
5. **The 200–400 byte estimate** is a sum of per-site guesses in a tree with
   **512 bytes of budget left**. The first thing that crosses will make it real.
6. **Whether the aspect change across the seam is tolerable in use** (§5).
   Nobody has looked at it — a card swap on the field machine is what would.
7. ~~**A PRE-EXISTING defect this work did not cause**~~ — **answered: it is
   not a defect at all, it is a hole in the emulator, and the kernel is
   right.** The report was that after clicking **Single** the second card is
   still scanning with the desktop dither on it, where `vid_disp_init`'s Single
   path does call `vid_blank_kind`.

   **MartyPC's MDA does not model 3B8h bit 3, the video enable.** Driving the
   port from the host on a Hercules os8088 has programmed — an *unprogrammed*
   one reads 0 frames and 0 lit whatever you write to it, which is what the
   first attempt at this measured and why it had to be redone after an extend:

   | 3B8h | meaning | measured |
   |---|---|---|
   | `0x0A` | graphics + video ON | 75 frames/0.7s, 367,488 lit |
   | `0x02` | graphics, **video OFF** | 76 frames/0.7s, 367,488 lit |
   | `0x00` | text, video off | **0 frames/0.7s** |

   `0x02` is what `vid_blank_kind` writes, and it is **indistinguishable from
   `0x0A`** here — same raster, same pixels. So no observation of that card can
   tell a blanked Hercules from a live one, and what stops the raster in
   MartyPC is leaving **graphics** mode (bit 1), not disabling **video**
   (bit 3). The kernel writes the architecturally correct value: bit 3 is the
   video enable on every MDA and HGC, gating it is what §39.18.1 specifies, and
   it is what keeps the CRTC running so the monitor never re-acquires. Writing
   `0x00` would "pass" here and is the defect `vid_blank_kind`'s own header
   records being **fixed** — it does not blank the card, it puts it into MDA
   text mode with a 6845 still carrying 720×348 timings.

   **The contradiction that made this look like a live bug is dated.**
   §39.18's `fsxdisp` really does record "3B8h bit 3 clear → 163 frames/s
   becomes 0" — taken at `a32be1d` (11 Aug), where the `0x02` fix landed at
   `e3e0fb2` (12 Aug). It is a measurement of `0x00` wearing bit 3's name. Its
   Hercules leg is therefore measuring the model rather than the kernel as
   well, and has not been re-run.

   `tests/dispmode.py`'s `dark()` is **tri-state** now: `True` dark, `False`
   live, `None` for a mono secondary, which it reports and does not judge. Both
   `dispmode` runs pass. **The mono half of this is the 5150's question** —
   one of the three things only a real monitor can answer.
8. ~~**A window cannot be dragged across the seam on a mixed-depth machine**~~
   — **THE FREEZE NO LONGER REPRODUCES**, after merging `elendilon`
   (`0ca93d6`). The one-command reproduction reaches the same point in the
   same script and the machine stays **alive**: `os88mouse` reads `mouse_x`
   back and reports `could not reach (1100,205): stuck at (979,205)`, which a
   frozen guest cannot do. The likely fix is **§39.14 / `1b3691f`**, which
   names this bug's call stack — `gfx_xor_rect_sw`, `gfx_save` and
   `gfx_restore` bank AX–DX around `GFXDENTER`/`GFXDORG` now, because *"the
   hook translates the rect into display-local coordinates and nothing puts it
   back"*, and a rect silently translated into another display's coordinates
   is exactly the wrong-context write §4 measured.

   **Not closed**: nobody has yet driven the original *manual* session across
   the seam on the merged tree and watched it survive. Do that, then delete
   docs/DUAL-DISPLAY-BUG2.md rather than editing it.

   **→ docs/DUAL-DISPLAY-BUG2.md is the handoff**, and its box at the top now
   carries the above. The rest of this item is the record of how it got there,
   kept because the ruled-out list is still worth having.

   What was established:

   - **It is a hard freeze, not a clamp.** The BIOS tick at `0040:006C` stops
     dead (measured 8406 → 8406 across half a second) and the CPU is found in
     the heap at `CS = 4782` with `IP` wandering — it has run off, not stalled.
   - **It is mixed-depth only.** The identical scripted drag on
     `os8088_5150_both_gla` with the **Hercules primary** crosses to x = 1019
     and always did; on `os8088_xt_vga_herc` it dies at the seam.
   - **The pointer clamp is correct and is not involved.** Button up, and
     button *held over bare desktop*, the pointer reaches (1100, 205) with
     `[cur_disp]` flipping 0 → 1, including rows only the taller display has.
   - **The trigger is exact**: the packet that carries the pointer across the
     seam leaves `[vid_cur] = 1` with `[vid_mono] = 0` and `[vid_rseg] = 0`
     while the primary is the Hercules, and the tick stops in that instant.
     Outside a drag the same crossing leaves `[vid_cur] = 0`, the cursor
     bracketing itself (§39.15.2).
   - **Two structural facts explain how that state is reachable**, and both are
     worth knowing on their own. `vga_xor_rect_vram` is the only drawing call
     in the kernel taking **virtual** coordinates that neither clips,
     translates nor selects a display — a transient overlay bypassing §11.3 on
     purpose, which nothing ever gave §39.14's split. And `ui_drag` is the only
     place drawing across **many passes inside one lock hold**, where
     §39.14.3's rule is that the last display drawn on stays current and
     **`gfx_unlock`** is what puts 0 back. Two 1bpp displays hide it entirely:
     the live words differ but the *renderer* does not.

   **Both fixes moved the failure rather than removing it:** `GFXDISP` on
   `vga_xor_rect_vram` cured the Hercules-primary freeze and broke the
   VGA-primary direction that had been working (`gfx_disp_run` keeps its rect
   and body pointer in **module scratch**, is not re-entrancy-guarded against a
   hooked body — its guard is `[gfx_dnest]`, which it never increments — and
   does not restore the display either); and pinning display 0 in
   `ui_drag_xor` survived a synthetic packet walk in both directions and broke
   both real `dispsave` drags.

   **Two further rounds; four more candidate fixes; still not fixed.** What is
   now established, and what was disproved:

   - **It is not the pointer clamp, not `vid_apply`, not the IVT, and not a
     stack overrun.** The clamp reaches (1100, 205) with the button held over
     bare desktop. A counter on `vid_apply` shows it never runs during the drag
     (the "cursor home" coincidence that suggested it was a **torn 16-bit
     read** of `mouse_x`: 719 → 819 caught as the new low byte with the old
     high byte = 563). `int 08`'s vector reads `0060:4741` before *and* after.
     And at the freeze `SP` is 22 bytes from healthy with `SS`/`DS` correct and
     `sch_stkdie` never entered, so the canary did not fire and nothing ran
     away down the stack.
   - **The registers at the freeze are `CS = 0, IP = 0x68, IF clear`** — the
     CPU is executing the interrupt vector table as code with interrupts off,
     having far-transferred through a zeroed segment. That is the *symptom*.
   - **The cause is a write through segment zero, and its signature is exact.**
     886 bytes of segment 0 change during one drag, and the commonest gap
     between them is **80** — the VGA's stride — while the write goes through
     `[vid_rseg]`, which is **0**, the VGA's value. So the *software* renderer
     is running against an otherwise-consistent **VGA** context: `[vid_mono]`
     alone disagrees. It XORs the drag outline into the kernel's own code (150
     of one 8 KB `.text` span), and the machine dies later, somewhere else,
     when something far-calls through what was overwritten.
   - **The display records and the live block are correct at rest.** Read after
     the primary swap: d0 = B000/90/720×348/kind 1, d1 = A000/80/640×480/kind
     0, live = mono 1, stride 90, rseg B000. So nothing is poisoned; the tear
     is transient and inside the drag.
   - **`cur_dprev` is a real defect found on the way, and is worth fixing on
     its own.** `CUR_DBEGIN`'s comment says the outgoing display is "banked on
     the stack"; it is banked in **one global byte**, and the header three
     lines above names the two contexts that then collide — the bracket is
     taken on the UI task by `gfx_lock`'s deferred hide *and* inside IRQ4. An
     interrupt landing in the task's bracket overwrites the outer saved value,
     so the task is left on the cursor's display. A nest-indexed replacement
     was written, tried **twice** — once among the other candidates and once
     entirely on its own, with the tree otherwise clean — and is **not** in the
     tree either time: it breaks `tests/dispsave.py --machine
     os8088_xt_vga_herc` deterministically, taking no raise cache at all for a
     window on display 1. The obvious explanation is disproved: `cur_dnest`
     reads **0** at every observation, so the bracket is balanced and the
     replacement is not merely correcting a drift that something downstream
     depends on. **Why a strictly more correct save breaks that gate is not
     understood, and is worth understanding on its own** — it is a small
     routine and the failure is deterministic.

   **What was tried and reverted, with the reason it is worth knowing:**
   `GFXDISP` on `vga_xor_rect_vram`; pinning display 0 in `ui_drag_xor` (twice,
   before and after the other fixes); making `vid_ctx_act` publish atomically
   with `pushf`/`cli`; and the `cur_dprev` nest. **The `cli` one was committed
   (a371ad5) and then reverted**: it closed a race that a call stack had
   directly evidenced — the *mono* arm running with `[vid_rseg] = 0` — and it
   broke `dispsave` on the VGA-primary machine, which had been passing.

   **The process lesson is the one to carry forward.** That regression was
   committed because only **byte identity on the three single-card adapters**
   was run before committing, and this whole area is invisible to that gate by
   construction: a single-display machine never calls `vid_ctx_act` at all.
   **`tests/dispsave.py` on both pairings, and `--swap`, are the gates that
   bind here** — nothing in `kernel/vidsel.inc` or the cursor's display bracket
   should be committed without them.

9. **`tests/dispsave.py`'s VGA+Hercules leg flips its verdict on DEAD BYTES,
   and until that is understood its failures are not evidence.** Open, and it
   outranks everything else in this list because it is the instrument two of
   the entries above were judged with.

   The measurement: at `bf83158`, adding **`times 100 db 0`** to `.text` —
   padding that cannot execute, in a routine nothing calls — takes the run
   from PASS to `wm_su_segs[0] = 0x0000`, *"no raise cache was taken for a
   window on display 1"*. Step 4's +100 bytes reproduce it exactly, and step
   4 touches nothing `dispsave` executes: `vid_text` is reached only from
   `CMD_REBOOT` and fsx, and `vid_6845_prog` is verified byte-identical in
   output on all three single-card adapters.

   **What it is NOT.** The memory ladder is identical in both builds —
   `KERNEL 0x0060  COLD 0x0de0  FAT 0x1680  LOW 0x17a0  HEAP 0x19e0`,
   `KERN_SIZE 104,448`, the same 512-byte image rung with 158 bytes of slack
   before and 58 after — so the heap base, every segment and `KERNEL.SYS`'s
   183 sectors are unmoved. It is not the ladder, and it is not the disk.

   **It is deterministic, and it is NOT an unstable test.** A/B'd with the
   images rebuilt clean before every run — which had to be established first,
   because **`dispsave` dirties the disk it boots**: its `close_panel` writes
   `SYSTEM.CFG` (§31.8), so its second run boots a machine that is already
   extended and no repeat is comparable to the first without
   `rm -f build/os8088*.img build/apps*.img && make` in between. Controlled
   that way it is **unpadded 2/2 PASS, padded 2/2 FAIL**.

   **And the failing assertion is a SYMPTOM, not the fault.** Reading the
   whole run rather than its verdict, the two builds differ *before* the
   cache is ever asked about — in where the test's own covering window
   lands:

   | | unpadded (PASS) | padded / step 4 (FAIL) |
   |---|---|---|
   | back window on display 1 | (663,80) 320×200 | (663,80) 320×200 — same |
   | front window **dragged to** | ~(723,120) | ~(723,120) — same |
   | front window **landed at** | **(719,119)** ✓ | **(855,196)** ✗ |

   **The window is STUCK there.** Driving the same drag four times in a row
   moves it not one pixel — `(855,196)` every time — so this is a clamp or a
   refusal inside `ui_drag`, not a dropped packet or a race with the tick.
   The two windows do still overlap at that position, so "no cache was
   taken" is not simply "nothing covered it" either.

   **ANSWERED, AND BY SOMEBODY ELSE: the gate was right and this reading of
   it was wrong.** `elendilon`'s **§11.96.11.2** (`82cf28c`) is the fault —
   `wm_su_ext`, the four band-extent bytes **per window SLOT**, was never
   cleared at `wm_destroy`, so a window landing in a reused slot inherited
   the last tenant's bands and `wm_su_flay` laid its cache out as fragments
   for bands it never asked for. Merged in, `dispsave --machine
   os8088_xt_vga_herc` passes. It was a real kernel bug all along, and one
   with nothing to do with this branch.

   **Two corrections worth more than the entry.** The first: *"the failing
   assertion is a symptom, the drag is the fault"* — stated here two commits
   ago — is **false**, and the merged tree disproves it in one line. The
   covering window still lands at **(855,196)**, the very position that was
   called the cause, and the cache is taken and the run passes. A difference
   that correlates with the failure is not the failure; the drop position was
   downstream of the same session-state divergence, not upstream of it.

   The second: **why a dead-byte pad flipped it now makes sense**, and the
   answer is not "size". Which slot a window gets, and what the previous
   tenant left in `wm_su_ext`, is a property of the *session* — and the
   padded and unpadded builds diverge earlier in the session (that same drop
   position), so they arrive at the covered-window check having reused slots
   differently. The padding perturbed a genuinely buggy code path rather than
   revealing a size dependence.

   **What still stands from this entry** is the harness fact, which is the
   part that will bite again: **`dispsave` dirties the disk it boots**, so no
   repeat of it is comparable to the first without
   `rm -f build/os8088*.img build/apps*.img && make` in between. And the
   smaller lesson: `dispsave`'s failure text *names* `wm_su_take`'s gate as
   the cause, which is a hypothesis it prints rather than anything it
   measured, and it sent this investigation at the wrong subsystem twice.

   **Item 8's `cur_dprev` verdict is still void**, for a reason that survives
   the correction: it was judged by this gate while this gate was reporting a
   real bug that depended on session state, and it adds bytes. Re-test it on
   the merged tree — docs/DUAL-DISPLAY-BUG2.md §7 is where that is written
   down.

10. **A DRAG cannot take the pointer past x = 979 with the mono card primary,
    on a desktop that runs to 1359.** Open, reproduced, and it is what is left
    of item 8 after the merge.

    `tests/dispsave.py --machine os8088_xt_vga_herc --swap` reaches the
    arrangement — `d0` Hercules 720×348 at (0,0), `d1` VGA 640×480 at
    (720,20) — and then `os88mouse` cannot drive the drag past **979**.

    **It is not `mou_clamp`** — button up, the pointer reaches 900, 1000,
    1100 and 1300 exactly. **And it is probably not a kernel clamp at all**:
    the field reports the cursor reaching every pixel of both screens, and
    item 11 shows the same harness failing to move the pointer at all after a
    click while the guest is plainly alive. A repaint on a 4.77MHz 8088 takes
    seconds and the 1200-baud UART drops a packet sent into it, which reads
    exactly like a clamp. **Re-test this against the harness before looking at
    `ui_drag`.**

    **979 is 720 + 259**, which is not a geometry this arrangement has: the
    secondary is 640 wide, so the far edge should be 1359. Whatever is
    answering 259 (or 260) is the thing to find. Worth checking first whether
    it reproduces with the **VGA** primary, which is the direction every
    other gate here passes on.

    The other `--swap` gate results are unaffected: `dispdrag` (elendilon's
    own straddling-drag test) **passes**, and so do `dispsave` on Hercules+CGA
    and on VGA+Hercules unswapped.

    **A second case joined it once §39.2.2 was fixed, and the order matters.**
    On the VGA-primary arrangement `tests/dispfreeze.py` now cannot take the
    pointer to `(439,199)` — it stops at `(440,99)`, deterministically, with
    the button **up**. That click *succeeded* on every run before the
    `vid_clk_hx` fix, which is the interesting part: the menu-bar overrun was
    writing ~80 bytes past `menu_bcell` into whatever follows it in `.bss`,
    so the pointer was reaching that row **because** something was being
    scribbled on. Removing the corruption did not create this; it stopped
    hiding it. Whether it is the same clamp as the x = 979 case above is
    unknown and worth asking first, since one fix may answer both.

11. **A single click on a Disk window froze or rebooted the machine.**
    **FIXED** — SPEC.md §39.14.10, `tests/dispcold.py`.

    Extended desktop, VGA primary and Hercules secondary; a B: Disk window;
    Note Pad dragged to straddle the seam; one click on a file row. The field
    saw it as a freeze first and a reset three times after; both are the same
    event.

    **The kernel was drawing into its own code.** `[vid_mono]` chooses the
    renderer, and on a two-card desktop it is a question about the display the
    renderer is *currently pointed at* — which only `GFXDENTER` points.
    `gfx_xor_rect` and `vga_xor_rect_vram` tested it BEFORE their display
    hook, which lives inside `gfx_xor_rect_sw`; every other primitive has the
    hook above the dispatch. So a dock tile's active ring on the VGA,
    dispatched while the Hercules was still current (§39.14.3 does not restore
    a display), chose the SOFTWARE renderer — and `GFXDENTER` then switched
    the live block to the VGA underneath it, where `[vid_rseg]` is 0. `sw_col`
    wrote through `ES:DI = 0960:8FC1`, flat `0x125C1`, which is
    `COLD_SEG:45C1`, which is `fm_layout`.

    The whole chain everything else in this entry was chasing follows from
    that: a `call far` became `cbw`, DS went to 0, `fm_layout`'s stores landed
    0x600 low across `ui_rebootq`, and `ui_task` restarted the machine
    honestly. Fix: both entries take the frame, `GFXDENTER`/`GFXDORG`, then
    `gfx_xor_rect_raw`; `gfx_xor_rect_sw` is gone. `.text` **−2 bytes**.

    **The gate is `tests/dispcold.py`, and the reason it works is worth more
    than the bug.** `.cold` holds no data directives (SPEC.md §2.6 rule 1,
    enforced by `tools/os88ovlchk.py`), so **every byte of it that differs
    from the built image is corruption** — where the same diff over `.text`
    has a 283-byte noise floor of initialised data and says nothing. Before
    the fix, one click left 34 differing runs in `.cold` at a **stride of
    80**, which is a 640-pixel scan line and is what identified this as a
    drawing operation rather than a stray pointer. After it: six clicks, zero
    writes, byte-identical.

    **Five instruments were wrong before one was right, and each failure is a
    general trap.**

    - **`0040:006C` IS THE WRONG CLOCK.** `sch_isr` chains the ROM's int 08h
      and bumps `[ticks]`; `ui_cmd_reboot` calls `sched_unhook` on its way to
      `int 19h`, so after a dispatched Restart the BIOS counter keeps
      advancing while `[ticks]` stops dead.
    - **…and `[ticks]` is the wrong clock for a FREEZE.** It is bumped from
      inside IRQ0, so it advances through a UI task that has stopped. The
      honest measure is **ui_task passes**, counted by a memory breakpoint on
      the byte its step 0 reads — a data access, so unlike an exec breakpoint
      it cannot fire on prefetch. `tests/dispfreeze.py` reports it.
    - **A MEMORY breakpoint stops LATE.** It trips inside the bus cycle and
      the CPU runs on to the next instruction boundary — sometimes taking an
      interrupt first (the first catch reported `sch_isr`'s entry with an
      interrupt frame returning to `ui_task+5`). So neither CS:IP nor the
      watched byte's own value is a sound filter: on the fatal pass the CPU
      ran through `mov byte [ui_rebootq], 0` before the host could look, and
      the one stop that mattered was indistinguishable from 70 benign ones.
    - **An EXEC breakpoint fires on PREFETCH.** One at `ui_task+7` — the
      instruction that runs only when the `je` falls through — stopped with IP
      still on the `je` at `+5` and ZF=1, because the 8088's queue had pulled
      the byte in.
    - **What finally answered it was a patch, not a measurement.** `cmp byte
      [ui_rebootq], 0` was rewritten in guest memory as `mov al, [ui_rebootq]`
      + `cmp al, 0` — the same five bytes, the same test, with the byte it read
      left in AL. **AL = 0xC8**: the flag was genuinely non-zero and was *not*
      the `1` that `ui_reboot_post` writes, which is what turned the search
      from "who posts a restart" into "what scribbles memory".
      **Prefer a change that makes the wrong answer impossible over a
      measurement that has to be lucky.**

    **Eliminated along the way, and none of them was the fault**: `drv_tab`
    (clean at every step), the window records (`W_DISP` is +20 and `W_SEG` +22
    in a 28-byte record — an earlier reading at +14/+16 was of `W_ONKEY`/
    `W_ONCLICK` and its "corruption" was an ordinary near pointer),
    `vid_ctx_act` (banks and restores DS and ES), `ui_reboot_post` (never
    runs), and the whole stack-overrun class — **`SCH_MAGIC` is only seeded
    under `KFZTRACE`**, and task 0 has no canary at all in a shipped kernel,
    so the earlier "0 hits in `LOW_SEG`" was a search for something that was
    never there.

    **Two guards landed on the way and are kept**, though neither fixes this:
    `drv_call` refuses a `DRVR_SEG` of 0 (its own declaration says "0 = not
    loaded" and it read straight past that into `call far`) and refuses a BX
    outside `drv_tab` — checked first, and on BX itself, because BX is the
    only thing there that does not go through DS.

    **§39.2.2's `menu_bcell` overrun is fixed and did NOT cure this**, and the
    verification once claimed for it was invalid: the harness click never
    landed and the tick that said otherwise was the BIOS's. That fix stands on
    its own arithmetic; the claim that it cured the field report does not.

12. **Mode X was refused when Missile Command was LAUNCHED from a window on
    the Hercules.** **FIXED** — SPEC.md §39.18.2, `tests/dispmodex.py`.
    Reported as *"Missile Command would not go into Mode X"*.

    **It is not gated on the CPU.** `fsx_capstab` is indexed by `VID_*` alone
    — VGA `0x01EF` (bit 8 = `FSXM_MODEX`), HERC `0x0011`, CGA `0x000F` — and
    neither `fsx_caps` nor `fsx_mode` consults `OSAPI_CPU_INFO`. The XT gate
    in Missile is `mc_adapter`'s **explosion ramp** (§48.8), a different
    question entirely.

    **It is gated on which display the asking window is on** (SPEC.md
    §39.18.1), which `fsx_caps` finds through `wm_top` — *"an app greying its
    own mode menu is frontmost by construction"*. **`mc_entry` breaks that
    construction**: `mc_adapter` runs at the top of the entry proc, dozens of
    instructions before `OSAPI_WM_CREATE`, so `wm_top` is still the window the
    game was launched FROM. Measured, same machine, same session:

    ```
    launched from a Disk window on the VGA        mc_caps = 0x01EF   Mode X live
    launched from the same window on the Hercules mc_caps = 0x0011   Mode X greyed
    ```

    The game's own window then opens on the VGA either way, so the menu item
    reads `Mode X (Vga)` on a machine that has one.

    **Nothing re-asks when the window MOVES between displays either.**
    `mc_adapter` is re-run by `mc_onresize` (§11.98) — a resize, not a move —
    so `mc_caps`, `mc_mono` and `mc_ecoarse` are all per-display facts held as
    per-machine facts. That is the same class as §39.14.10's `[vid_mono]`.

    **Both halves were taken.** `fsx_caps` takes `BX` = the window to ask
    about (0 = frontmost, for a caller with none) and stops guessing — a
    contract change at a live number, which §20.8 rule 4 permits while this
    tree hosts every caller; there were two. And Missile asks about its own
    window every frame from the worker (`mc_dispck`, Arkanoid's §44.8
    pattern), re-running `mc_adapter` only when the answer differs, so the
    facts follow the window as it moves.

    **The depth had to come from the same call.** `OSAPI_VIDEO`'s `DH` is the
    **primary's** (§39.2.1) — right for sizing a window, wrong for this — so a
    Missile dragged onto the Hercules kept drawing §48.8's fine explosion ramp
    on a 1bpp card, the measured unplayable case. `fsx_caps` already returns
    `DL` = the answering display's kind, so one call gives both and they
    cannot disagree.

    Cost: kernel `.text` **+4 bytes**, no rung crossed. Verified —
    `tests/dispmodex.py`, four cases: launched from the VGA `0x01EF`/mono 0;
    dragged so its **centre** is on the Hercules `0x0011`/mono 1; dragged back
    `0x01EF`/mono 0; and launched from a Disk window on the Hercules, where
    the entry-time answer is corrected on the worker's first frame to
    `0x01EF`/mono 0. `tests/dispapps.py` covers the single-display path and
    the VGA↔CGA adapter switch through the new derivation.

    **Harness note worth keeping**: the first run of this measured `0x0000`
    from the Hercules and it was the test, not the kernel. The first launch
    leaves the Disk window *inside* `GAMES`, so a second pass at the same row
    numbers opens ARKANOID and SOLITAIR — and reading Missile's bss offsets
    out of another package's segment answers a plausible zero rather than an
    error. `tests/dispmodex.py` takes the `..` row first and prints how many
    package windows are up.

13. **Fullscreen with the window's centre on the Hercules gated the pointer
    to that screen's right edge, and coming out only drew the VGA side.**
    **FIXED** — SPEC.md §39.18.3, `tests/dispmcfs.py`.

    One cause, and a third defect nobody had reported came with it.
    `fsx_run` collapsed the machine to a single display at the virtual
    origin **unconditionally**, including for a same-mode bracket — which is
    what §53.7 promises will not happen (*"no `fsx_mode` call, so the drawing
    slots stay legal and the geometry is still the desktop's"*). On the
    secondary display that is a 640-pixel lie, and everything that lives in
    virtual coordinates walked straight into it: the pointer (940 → 360 →
    **320**, i.e. it ended the round trip on the other monitor), the
    fullscreen window's rect (the HUD drawn **twice**, once at each
    interpretation), and the desktop on the way back.

    The collapse moved to `fsx_mode`, where a mode is actually set — the only
    thing that makes another display's geometry meaningless. A bracket that
    sets no mode now changes nothing about displays at all.

    Cost: `.text` **+21 bytes**, `.bss` +1, no rung crossed. Verified —
    `tests/dispmcfs.py`: the pointer stays virtual and moves (935 → 815, both
    on the Hercules); after the round trip **both cards are byte-identical to
    a forced full repaint**; and the mode-setting path still collapses
    (`vid_ndisp` 1, `fsx_cur` = `FSXM_MODEX`) and gives the second display
    back on exit.

    **The assertion took three tries and the failures are the lesson.**
    Counting lit pixels before and after says nothing — Missile keeps playing
    inside the bracket, so its own content legitimately differs, and the first
    version read that as a failure. Excluding the window's rect was no better:
    the window is at virtual `(640, 20)` on that display and translating only
    x left twenty rows of it inside the "desktop" region, and the window's
    rect *changed* across the trip anyway (`wm_fit` clamped a window that had
    been dragged past the Hercules' bottom edge). **Stale means "differs from
    a fresh repaint"**, so the sound test is to force one and compare — no
    exclusion box, and it needs no theory about what should have changed.

    **And one apparent kernel bug in the middle of it was the harness.**
    `[vid_ox]` and `[vid_cur]` read as disagreeing about which display was
    live — `cur=0` with `ox=640`. `vid_ctx_act` writes the origin as two
    stores, Missile's worker draws continuously, and the reads were taken on
    a *running* guest: it was being caught mid-update. `tests/dispmcfs.py`
    pauses before it reads anything. Same trap as 8(11)'s register dumps.

14. **Paint's GIF render was ~10x slower with the desktop extended.**
    **FIXED** — SPEC.md §39.14.7.1, `tests/paintgif.py`.

    *"Build 674 renders `OS8088.GIF` in roughly a second, on 704 it takes
    more than 10."* Two things narrowed it fast and both are worth copying.
    **Paint's binary is byte-identical between the builds** (`md5sum
    build/paint.o88`), so it is kernel-side and no app-level theory need be
    entertained. And the field pinned the condition: it needs the desktop
    **extended**, and Paint's window is **wholly on one monitor** — so it is
    not the straddling case and not the second display's drawing.

    **It is not new code.** §39.14.7 — removing `gfx_blit4`'s whole-shape hook
    so a straddled canvas draws on both cards — is already in elendilon,
    byte-for-byte. What this branch added is the ability to *extend a VGA and
    a Hercules*, which is what made the cost reachable. The 674 measurement
    was a machine that could not extend.

    **What §39.14.7 got wrong is its own price.** It reasoned "per RUN,
    `gfx_disp_run` instead of two compares, against a call that already costs
    ~756us" and flagged the figure as modelled. But the guard it left behind
    reads `cmp byte [vid_ndisp], 1 / ja .slow`, and `.slow` is not a slower
    way to write a run — it is **giving up §5.4.1's fast path**, so every
    coalesced run became a whole `gfx_fill` instead of a direct framebuffer
    write.

    Measured on a cycle-accurate XT with both cards, the same scripted open
    through the association: **one display 34.44 s, extended 48.62 s,
    extended with the gate 33.35 s.**

    The gate is §39.14.7's own "considered and NOT taken" option, and it is
    smaller than the version that was weighed: only `AX`/`BX` need
    translating (`CX`/`DX` are a width and a height, which no origin moves),
    and the straddling case is not a second path — it is the one that was
    already there, reached by falling through. `.text` +69, `.bss` +1, no
    rung crossed. `tests/dispblit.py` (straddled) still passes untouched, and
    a canvas dragged wholly onto the Hercules renders complete.

15. **"Switching Tracker to full screen then back corrupts the other
    screen."** **FIXED** — SPEC.md §53.7.1, `tests/dispfsx.py`.

    **The round trip is not what is broken, and four runs said so before
    anything was changed.** Tracker's fullscreen on a tier-0 machine is XT
    mode's 80x25 TEXT screen (§45.13) — `FSXM_TEXT80`, a *mode-setting*
    bracket, which is exactly the path 13 above moved the collapse into — so
    that was the suspect. It is clean: VGA-primary and Hercules-primary,
    window on either display, both cards come back **0 differing pixels**
    against a forced full repaint, with the raster geometry and both `vid_ctx`
    records unchanged across the trip.

    **What is broken is the SAME-MODE bracket, and only while it is up.**
    Turn XT mode off (`X`) — which is also every machine above tier 0, since
    XT mode is only pre-armed on an 8088 — and `F` is §53.7's "exclusive but
    same mode": no `fsx_mode` call, so by 13's fix nothing collapses, so the
    coordinates are still the whole virtual desktop's and **(0,0) is the
    primary**. Tracker's `tui_track` answered the fullscreen case with
    *(0,0) plus `osapi_video`'s size*, so with its window wholly on the
    Hercules at x 767..1186, pressing **F** took the **VGA** from 189,477 lit
    pixels to **95,619** — the FT2 screen, on the monitor Tracker is not on —
    while the Hercules sat unchanged at 152,778 showing the desktop it
    already had. Paint's `pt_org` had the identical line and the identical
    defect.

    **It is invisible to a round-trip test by construction**, which is why
    the first four runs passed: §53.6's exit `wm_paint_all` repaints the
    world, so what this costs is the whole of the fullscreen session and
    nothing after it. The assertion that catches it is *which card changed
    while the bracket was up* — the bracket's own display must change a lot
    and the other must not change at all — and it is a different question
    from 13's, not a better version of it.

    Verified after: **140,747 pixels on the bracket's own card and 0 on the
    other**, Tracker and Paint alike, both cards still pixel-identical to a
    forced repaint afterwards.

    `fsx_surf` (slot 0x03F8) hands the app the rect its bracket owns, in the
    coordinates the drawing slots take. It answers `(0,0,w,h)` on every
    one-display machine and after any `fsx_mode` call, so it is exactly what
    the two apps hard-coded and a single-display machine cannot see the
    change. Missile Command needed nothing: it stacks §11.2, and
    `wm_fullscreen` sizes the window to the display it is on (§39.17.1), so
    its geometry came out of the window record and was already right.

    **Two things came with it, because a display is not only a rect.** The
    layout Tracker picks by screen HEIGHT needs re-picking for a surface of a
    different shape (a 480-row VGA layout on a 350-row Hercules loses its
    bottom third), and the DEPTH cannot come from `osapi_video` either — it
    answers about the primary (§39.2.1) — so it comes from `fsx_caps`' `DL`,
    which is already *that display's* kind (§39.18.2).

    Cost: `.text` **+70 bytes**, no rung crossed (the image rung's slack
    327 → 257). Every `.o88` is invalidated by the slot append, which §20.8
    rule 4 makes a rebuild rather than a compatibility event.

---

## 9. If the answer is "not now"

Two things in here are worth doing regardless, and neither depends on the
feature:

- **§1.3's correction.** `vid_blank`'s "a pairing nobody built" is factually
  wrong — VGA/EGA + MDA is the classic period dual-monitor configuration — and
  SPEC.md §39.13 rests a design decision on it. The hole that comment describes
  is already closed by `vid_blank_kind`'s VGA arm. Correcting both costs nothing
  and stops the next reader inheriting it.
- **§3's predicate.** `vid_dual_ok` refuses `VGA | HERC | CGA` on reasoning that
  does not hold, and expressing the rule as *"a mono card plus a colour card"*
  is smaller and clearer than the equality it replaces even if nothing is ever
  extended onto a Hercules. It would then simply be a predicate that is right.
