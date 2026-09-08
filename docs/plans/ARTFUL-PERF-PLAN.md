# ArtfulType performance plan
**apps/artful/ — SPEC.md §46. Target: IBM PC/XT, 8088 @ 4.77 MHz.**

Geometry, read from `at_geom_init` (apps/artful/atrend.inc:56-78) and `at_rowhtab` (apps/artful/artful.asm:864), because every figure below is per-adapter and the three differ by 2.6x:

| adapter | `at_tw` | ty0..ty1 | region | body rowh | full lines | cells/line |
|---|---|---|---|---|---|---|
| Hercules 720×348 | 592 | 44..328 | 284 | 10 | 28 | 74 |
| CGA 640×200 | 512 | 30..196 | 166 | 10 | 16 | 64 |
| VGA 640×480 | 512 | 44..460 | 416 | 10 | 41 | 64 |

---

## 1. WHERE THE TIME GOES

**One visual line is one `OSAPI_GFX_BLIT4` and four package loops, and the two cheapest-to-delete of them are more than half of it.** `at_draw_line` (atrend.inc:963) runs `at_parse`, `at_compose`, `at_expand` and one blit. On Hercules that is PREDICTED at_parse ~5 ms + at_compose ~53 ms (74 × `at_glyph`, which I counted at 83 instruction bytes per source row → an 8088 fetch floor of 83 × 4.34 = 360 clocks × 8 rows + ~350 of prologue ≈ 3,350 cycles = **700 µs a cell**) + at_expand 23.9 ms (740 iterations of a 34-byte / ~154-clock loop) + blit4 40.4 ms (5,920 px × 32.6 cycles/px, **MEASURED**, PERFORMANCE.md:1283) = **~122 ms a line**. CGA is ~107, VGA ~186 — VGA worst, because a text row's first run is a glyph's leading blank column, under `gfx_blit_thr` = W/32 = 16, so every inked row takes §5.4.1.3's planar decoder at 106.9 cycles/px (**MEASURED**, Set 107) instead of the run writer.

**`at_expand` + `gfx_blit4` are 64 of those 122 ms and on a 1bpp adapter they carry no information.** `at_compose` has already built a 1bpp band in `at_strip1`; `at_expand` (atrend.inc:894) widens it to packed 4bpp purely so `gfx_blit4` can pack it straight back down in `sw_blit_row`, and §46.4.1's `at_codebg` has already *cleared* `at_cellbg` on 1bpp, so there is provably no second colour left in the expansion. This is the identical round trip PERFORMANCE.md:1278 records as **MEASURED and fixed in Paint at 22x — 809.4 ms → 36.6** by `OSAPI_GFX_BLIT1`, which is a `rep movsw` a row at a **MEASURED** 3.9 µs/byte over a ~395 µs intercept (Set 64).

**A keystroke is not one line, it is `at_rlk` lines.** `at_apply_edit` (atedit.inc:344-348) redraws `[at_dfrom .. at_dfrom+at_rlk-1]`, and `at_relayout` sets `at_dfrom` from `at_lhome` — the edited **paragraph**'s first visual line (atdoc.inc:995). §46.1's contract says so honestly, but it means typing at the end of a six-line wrapped paragraph is 6 × 122 = **732 ms a character** on Hercules, ~1.1 s on VGA. Three independent scouts tried to narrow that range and **all three predicates were refuted** (see part 5) — the range is real work until the per-line cost comes down.

**Any line-count change repaints everything below it, and the scroll bar twice.** `.below` → `at_redraw_below` (atedit.inc:411) is one full-region `GFX_FILL` plus one `at_draw_line` per visible line to the bottom, then `at_sbar`, then `.see` → `at_seecaret` → possibly `at_scroll_to` → `at_sbar` again. A Return mid-view on Hercules is ~97 ms of fill + 14 × 122 + 2 × ~140 = **~2.1 s**.

**`at_sbar` (atrend.inc:1443) is 21 far calls whatever moved, and it fills 286 gutter rows *before* the `jz .out` that decides the bar is not needed at all.** I counted the calls line by line: 4 `SET_COLOR` + 2 `GFX_FILL` + 4 `GFX_FRAME` + 10 `GFX_HLINE` + 1 `GFX_FILL_GRAY`; `gfx_frame` is four `gfx_fill` strips (kernel/vga12.inc:4024), so 19 kernel rects over ~1,226 scan-line setups ≈ **120-215 ms PREDICTED**, best point ~140. It runs on every arrow-repeat sample and every thumb-drag sample.

**`at_scroll_to`'s `cmp bx, 3` (atrend.inc:1279, :1286) sends any move past three lines to a whole-page repaint** — 97 ms of fill plus 28 lines, ~3.5 s on Hercules — where `gfx_scroll` over the same region costs ~122 ms (Set 56's **MEASURED** 269 µs per 32-byte Hercules row, scaled to 74) plus the revealed lines only.

**Layout walks the edited paragraph three times per keystroke.** `at_relayout`'s `.findnl` (atdoc.inc:1009) reads every byte through `at_getb` only to find the newline `at_scan` then rediscovers at `.endline`; `at_scan`'s `.ch` reads them again; `at_respan` (atdoc.inc:791) re-walks each wrapped line to re-derive a nibble both break paths already know. For a 400-character, 6-line paragraph that is PREDICTED 17.6 + 36.1 + 26.8 = **~80 ms a keystroke**.

**Where the scouts disagreed, and which reading I trust.** (a) *The per-pixel blit4 rate*: PERFORMANCE.md gives 32.6 cycles/px from the Paint-uncover decomposition and, in the sea-life row, ~2,863 cycles a **row** near enough whatever the width. Both are measured; they describe different regimes. I use 32.6 cycles/px for a full-width line (512-592 px, where the per-row floor is 12% of the total) and the ~2,150-cycle row floor for anything narrow — quoting per-pixel for an 8 px cell is what made one proposal's headline wrong by 5x. (b) *`at_getb`*: SPEC.md §46.9 and PERFORMANCE.md:459 both say "~32 clocks a character". That figure counts only the segment dance and omits the gap branch and the `call`/`ret`; the routine is 16 bytes plus a 3-byte call, so ~160/~205. PERFORMANCE.md's own table already flags it as written down rather than measured, and it must be corrected before it is quoted anywhere. (c) *`at_sbar`'s share of a scroll step*: the "84% of the far calls" framing is true and misleading — far calls are 1.0 ms of it. The bar is ~36% of a scroll step's **milliseconds** today.

---

## 2. WAVE 0 — THE MEASUREMENT FIRST (not optional)

There is **no `artful` row in tests/suite.py** (296 rows, checked) and nothing in PERFORMANCE.md measures this app. Every millisecond in part 1 and part 3 is PREDICTED. CLAUDE.md rule 4 — measure before redesigning, a counter is not a timer — and MartyPC lets it be done with **zero guest bytes**.

**W0.1 — resolve the package's symbols.** `tests/editmove.py:96`'s `pkg_syms` re-assembles `apps/artful/artful.asm` with `[map symbols]`; the bss names are `equ os88_image_end + N` behind macros and are neither greppable nor stable. The package segment is `os88geom.winptr(m, w.i, S) + os88geom.W_SEG` (tests/editmove.py:160). A breakpoint takes a **flat** address, so a package label is `seg*16 + offset`. `m.sym()` resolves kernel symbols only.

**W0.2 — freeze the caret worker before anything.** `at_worker` (artful.asm:581) XORs a cell every 9 ticks and lands inside every capture as pure `transient`. Poke `[at_drag] = 1` — `at_click_text` is its only writer and the worker's `.gate` (artful.asm:616) its only reader — and pin `[at_cphase] = 0`.

**W0.3 — the primitive CENSUS per keystroke, three adapters.** `os88marty.bp_count(m, target, act)` (tools/os88marty.py:2279) counts distinct entries and dedupes on the guest's own instruction count. Targets: kernel `gfx_blit4`, `gfx_fill`, `gfx_hline`, `sw_blit_row`, `vga_blit_span`, `gfx_xor_fill`, `gfx_scroll`, `font_run`; package `at_draw_line`, `at_parse`, `at_compose`, `at_glyph`, `at_expand`, `at_getb`, `at_relayout`, `at_span`, `at_respan`, `at_sbar`. Take it at the **end of a one-line paragraph** and again **mid-way through a six-line wrapped one** — those are two different numbers, not noise. Expected: `at_draw_line` = `at_rlk`, `at_parse` = `at_rlk` + 1, `sw_blit_row` = 10 × `at_rlk` on 1bpp.

**W0.4 — cycles, end to end.** `at_onkey` (artful.asm:367) entry and its final `ret` as two `bp_exec` addresses; `m.status()["cycles"]` is free-running (docs/MARTYPC-DEBUG.md:1314), so every reading is a delta; `ms = (c1-c0)/4772727*1000`. **`m.status()["state"]` reads `"breakpoint"`, never `"paused"`** — five investigations in this repo concluded breakpoints do not fire and every one was that poll. Run on `os8088_5150_cga_gla`, `os8088_5150_herc_gla` and `os8088_xt_vga`. Also bracket `at_glyph`, `at_expand`, the blit and `at_relayout` separately — the four-way split is what decides the wave order stays right.

**W0.5 — read `[at_rlk]` and `[at_dfrom]` after each keystroke** (two 2-byte guest reads inside W0.4's loop) at paragraph lengths of 1, 2, 4 and 8 visual lines. This turns §46.1's "that paragraph's visual lines" into a table, and it is the number every "×K" in part 3 multiplies by.

**W0.6 — the flash and the visible-redraw span.** `m.flicker(frames=90)` per PERFORMANCE.md Part 3.1 (tests/deskbench.py:179 is the worked example): park the pointer outside the text column, check `r['settled']` before believing anything, read `changed` for the span and `transient` **with its bbox**. Take it for a keystroke, a Return, a 4-line scroll and a W_PAINT. This is the only instrument in the tree that can see the whole-page white-then-reletter that `at_draw_text`'s leading fill produces (docs/TESTING.md:247 marks QEMU ❌). **Hercules cannot be flickered** — MartyPC's MDA never rasterises Hercules graphics mode — so take the flash on CGA.

**W0.7 — register the row.** `Row("atkey", "soak", py("tests/atkey.py"), <secs you measured>, ..., needs=("marty",), serial=True, alone=True)`. It asserts its **scene** (which document, which paragraph, `at_nlines`, `at_top`, the adapter) and prints numbers; a threshold that fails a build teaches nobody anything. Per docs/WRITING-TESTS.md §1, break it on purpose first.

**W0.8 — the A/B mechanism, before any code moves.** Two working-tree states cannot both be held: `tools/os88build.py:521` keys a private tree by its **make args**, so `tree()` with no args and a dirty tree is the same directory twice. Follow `NOHEDGE` (Makefile:1650, 2954-2960) — the precedent for a knob reaching a non-kernel binary: its own `$(ATDEF)`/`$(ATSTAMP)`, in `$(KNOBS)` for `make test-full`'s matrix, **out of `$(VIDSTAMP)`** because it touches no kernel byte. Each wave below ships with its `NOAT*=1` arm, which is also the only thing keeping the old path assembling.

---

## 3. THE WAVES

Every wave: independently shippable, independently measurable, SPEC.md edited **before** the code. ArtfulType is a package — image 19,257, image+bss 39,057 of `APP_MAX_SIZE` 61,440 (§46.9), ~22 KB spare — so no `KERN_BUDGET`, no 512-byte rung. CLAUDE.md's "design for bytes" still binds.

---

### WAVE 1 — The emit: hand the composed band to `OSAPI_GFX_BLIT1`
*Biggest and most certain. The only proposal here with a measured end-to-end analogue in this repo.*

**Proposals:** S17 (verified CONFIRMED), with S9's ink-width trim deferred to Wave 4.

**Files:** atrend.inc:975-996 (`at_draw_line`), :583 (`at_compose`'s strip clear), :811-819 (`at_glyph`'s `.vrep`), :874 (`at_ruleat`); artful.asm:907-916 (`AT_X4TAB`'s hv/lv arms, so the surviving 4bpp fallback reads the flipped strip); atui.inc:1262 (`at_bigtext`'s clear), :1313-1370 (`at_drawimg`); atimg.inc (regenerate complemented).

**The change.** `at_compose` already builds exactly the object `gfx_blit1` takes: 1bpp, row-major, bit 7 leftmost, stride `AT_S1ST` = 80, at `at_tx0` = 64 (a multiple of 8) with `at_tw` masked `and ax, 0FFF8h` (atrend.inc:59-62), `at_prh` ≤ 30 against the slot's 255. **Every argument rule is already satisfied.** Compose in *screen* polarity — a set bit is paper — then set `ES=DS, SI=at_strip1, BP=AT_S1ST, AX=[at_tx0], BX=[at_dly], CX=[at_tw], DX=rows` and call slot 0x0418, `jnc .done`. Gate: take the band arm when `[at_vbpp] <= 1`, or when `[at_vbpp] > 1` **and** the line has no code span (`[at_pcb0] > [at_pcb1]` — the compare `at_codebg` already makes at atrend.inc:677). Keep `at_expand` + `gfx_blit4` verbatim as the CF=1 and colour-code-span fallback: **`gfx_blit1` is `stc`/`ret` on kern_small** (kernel/kernel.asm:6388) and ARTFUL is not in Makefile:7174's `SMALLOMIT`, so it ships there.

No `OSAPI_GFX_BLIT1_PEN` call is needed: on 1bpp the pen is not read at all, and on VGA the default pair (ink `CWHITE`, paper `CBLACK`) short-circuits at `.pvga`'s `cmp ax,(CBLACK<<8)|CWHITE / je .pdone` with no port write.

**Expected win (PREDICTED from MEASURED unit rates).** Hercules line: at_expand 23.9 + blit4 40.4 = **64.3 ms → 3.29 ms** (740 band bytes × 3.9 µs + 395 µs intercept, Set 64) — **19.5x on the stage**, and the whole line 122 → **61 ms**. CGA 55.7 → 2.90, line 107 → **54 ms**. VGA 20.7 + 114.7 = 135.4 → 2.90, line 186 → **53 ms**, **47x on the stage** — the three adapters converge, which is itself the check that the arithmetic is right. Whole Hercules page 3.5 s → **1.8 s**. Splash artwork (`at_drawimg`, already 1bpp in the image, atimg.inc:1-3): ~180 ms → **6.6 ms**, once per windowed W_PAINT. In-tree precedent: **MEASURED 22x** in Paint (PERFORMANCE.md:1278) and **8.1x** on the saver's arrival (§79.5.8).

**Byte cost.** PREDICTED **+100 to +115 .text**, 0 bss. Nothing is deleted — `at_expand` (99), `at_x4w`/`at_x4g` (2,048 image) and `at_strip4` (9,120 bss) all stay as the fallback. Image 19,257 → ~19,360; check `apps360.img` still fits.

**SPEC.md first.** §46.4 ("*the expander widens 1bpp to packed 4bpp … one `OSAPI_GFX_BLIT4` delivers the line*", SPEC.md:59968) must name `gfx_blit1` as the path and `gfx_blit4` as the kern_small/colour fallback, and must state the strip's new polarity **as a contract**. §46.4.1 gains the sentence that a cleared `at_cellbg` is now what *qualifies* a 1bpp line for the band arm. §5.4.2/§5.4.2.5 gain ArtfulType as a named consumer — and §5.4.2.5's kern_small refusal now has a customer whose cost is on the **keystroke** path rather than a repaint; say so there rather than silently. atrend.inc:5-13 and atimg.inc:1-3 both assert the old design and both are wrong after this. `checkdocs.py` runs on every `make`.

**Hazards.**
- **The polarity flip has FIVE writers into `at_strip1`, not four.** `at_compose`'s clear, `at_glyph`'s `.vrep`, `at_ruleat`, `at_bigtext`'s own `xor ax,ax / rep stosw` clear (atui.inc:1262-1266 — the one the proposal missed, and it makes the splash title a solid black block), and `AT_X4TAB` is a *reader* that must be swapped to match. A missed one is inverted text, not a crash.
- **The `not` must go between `.noshear:` (atrend.inc:808) and `.vrep:` (:811), not earlier.** The bold overstrike (`shr ah,1 / or al,ah`, :740) and the italic `rcr` chain (:800-806) both assume ink polarity — `rcr` shifts in CF=0 meaning *no ink*. Complement any earlier and every italic glyph grows a black bar down its left.
- **`at_grow+3` is never explicitly written on the scale-1/2 paths** — only zeroed by `mov word [at_grow+2],0`. Both `not word` instructions are load-bearing; complementing only `at_grow`/`+1` leaves a 16 px black tail on every glyph.
- **Ordering:** `gfx_blit1` refuses `DX=0` with CF=1, which would fall into the blit4 fallback and draw a full-height line off the bottom of the region. Keep `at_draw_line`'s row clamp (atrend.inc:977-986) and its `jmp .out` strictly **above** the new arm.
- **`at_codebg`'s `not` (atrend.inc:676) is polarity-agnostic and stays correct.** Verify, do not assume.
- **Exercise kern_small.** `make small && make smallapps` and drive it: the *fallback* — `at_expand` reading the swapped tables over the flipped strip — is that build's only path and no kern_big test touches it.

**Measurement that proves it.** W0.3's census: `at_expand` calls per keystroke → 0 on 1bpp, `gfx_blit4` → 0, `gfx_blit1` → `at_rlk`. W0.4's per-line bracket on all three adapters. And a **pixel-identity gate**: the same document rendered with `NOATBLIT1=1` and without must be 0 differing pixels, on Hercules, CGA and VGA, with and without a code span, plus the splash and the About alert.

---

### WAVE 2 — Three near-free structural cuts
*Best win/effort in the set. Two are net-negative bytes or single digits. Independent of Wave 1, and each independently shippable.*

**Proposals:** S4 (CONFIRMED), S3 (CONFIRMED), S2/S20 (PLAUSIBLE, the two scouts agree on mechanism).

**2a — Decide the view before drawing it. Four sites paint the page twice.** (S4)
`atcmd.inc:501-511`, `atcmd.inc:593-604` and `atedit.inc:1789-1800` are **byte-identical 30-byte tails** (verified) — clamp, `at_caret_off`, `at_draw_text`, `at_sbar`, `at_seecaret`, `at_caret_on` — and `at_seecaret` can call `at_scroll_to`, whose `cmp bx,3 / ja .full` lands straight back on `at_draw_text` and `at_sbar`. Split `at_seecaret` into `at_seecaret_t` (the clamp arithmetic, returns the target top in AX, draws nothing) and today's body as a wrapper; each site becomes clamp → `at_seecaret_t` → `mov [at_top],ax` → `at_caret_off` → `at_draw_text` → `at_sbar` → `at_caret_on`. **One repaint, one bar.**
- Win: a View>Markdown toggle with the caret off-view is **104 far calls → 52**, exactly halved. Hercules ~3.6 s → ~1.8 s; VGA ~7.7 → 3.9; after Wave 1, ~1.8 → 0.9 s. It is also a correctness improvement — today the bar is drawn once with the pre-scroll thumb.
- Bytes: **−25 to −30 .text** if the three tails are factored into one shared proc (30 × 3 = 90 bytes become 34 + 9); +15 to +25 if they are not.
- Hazard, and it is the whole of the effort: `at_apply_edit`'s `.see` tail is shared by **three** arms (`.rng` at atedit.inc:344, `.bok` at :356, `.fdraw` at :371). **Only `.fdraw` may take the hoist** — the two incremental arms must keep the full `at_seecaret`, because there `at_scroll_to`'s ≤3-line blit arm is the correct cheap behaviour and replacing it with an unconditional `at_draw_text` is a large regression on the typing path. `at_seecaret_t` must return AX = `[at_top]` on the already-visible exit (:1400) or the view corrupts silently. Do **not** fold `at_fs_enter` (artful.asm:515-523) in — it carries the same clamp but is followed by `OSAPI_FULLSCREEN`, not the paint tail.
- SPEC.md §46.1's "*only mode/zoom switches, undo restores and W_PAINT redraw the whole page*" is true and incomplete: it does not say those paths can redraw it **twice**. Write the ordering rule (decide the view, then draw once) into §46.1 first — there is nothing today that would catch a fifth site getting it wrong, and three identical tails are evidence that copy-paste is how a fifth arrives.

**2b — Raise `at_scroll_to`'s threshold. The arithmetic says a page, not three lines.** (S3)
Replace both `cmp bx, 3 / ja .full` (atrend.inc:1279, :1286) with a compare against `at_vislines` (atedit.inc:786 — it already exists and is already what the page-click path uses). Do **not** latch a count in bss: `at_lgeom` is per-line and reads `at_lattr[bx]`, `[at_zoom]` and `[at_writer]`, so a latched word goes stale on zoom, on the Writer/Markdown toggle, on a heading edit and on every scroll.
- Win: `gfx_scroll` copies `height − |dy|` rows, so it gets **cheaper** as the jump grows, while `at_draw_text` costs the fill plus every visible line. Break-even is at or past the whole page on all three adapters. Hercules 10-line scroll today = 97 ms fill + 29 × 122 = **3.6 s**; after = ~122 ms scroll + 10 × 122 = **1.3 s**, 2.7x. After Wave 1+3 it is 0.7 s → 0.33 s. Honest scope: a **scroll-bar page click** is `at_vislines−1` and sits *on* the break-even (~11%); the beneficiaries are **thumb drags** (atedit.inc:1274) and `at_seecaret` jumps of 4..N−2 lines after a find, a click or a paste.
- Bytes: **+5 to +8 .text**, 0 bss.
- Hazard: `at_sumn` (atrend.inc:1355) accumulates dy in a 16-bit DI with **no overflow guard**. Do not remove the bound outright — a jump across a long document wraps dy into a small plausible value that `gfx_scroll` **accepts**, which is silent corruption rather than a refusal. `at_vislines` is 17-42, so a bounded threshold is safe; keep it bounded. `at_vislines` clobbers AX, which the up arm still needs. Check the CGA arm on the glass — the 166-row region and the interleaved-bank mono copy path have never been given a near-page dy from this app.
- SPEC.md §46.1 states the three-line rule verbatim ("*for moves of up to three lines*"); it, atrend.inc's file header and `at_scroll_to`'s own header comment (:1259) all change together, SPEC first.

**2c — Bank what is on the glass: `at_sbar` draws the thumb, not the bar.** (S2/S20)
Split into `at_sbar_full` (today's body) and `at_sbar_thumb`, banking three words: `[at_sbshown]`, `[at_sbmax]` (the `at_maxtop` it was drawn for) and `[at_sbty]` — **the thumb y that was actually DRAWN**, never one recomputed from a possibly-stale `[at_top]`. Then: nothing moved → 0 far calls; thumb moved → grey the vacated shaft strip + the two 1px frame columns + the thumb's fill and frame; maxtop or visibility moved → the full body. `at_scroll_to`:1343 takes the thumb form; W_PAINT, `at_refit` and `at_geom_init` take the full one and poison the bank. Two separate defects fall out in the same routine: **(i)** the 16×286 gutter `GFX_FILL` (:1451-1459) runs *before* the `or ax,ax / jz .out` (:1460-1462), so a document that fits pays ~32 ms to blank an already-blank gutter on every Enter and every wrap — gate it, and on a drawn bar replace it with two 14×14 arrow-box interior fills, which are the only pixels it uniquely contributes; **(ii)** `at_maxtop` is walked twice per call (:1460 and again inside `at_thumby` at :1593) — pass it in.
- Win: 21 far calls → **0 / 5 / 19**. ~140 ms → ~9-14 ms on a move, ~0 when the thumb has not moved — which on a 1,000-line Hercules document is 3 scrolls in 4 (travel is 237 px over ~972 tops = 0.24 px a line). The **undersold** part: `at_sb_repeat` (atedit.inc:1210) calls `at_linger`, which spins to the next 18.2 Hz tick — the arrow-repeat loop is currently **bar-bound at ~140 ms a pass and becomes linger-bound at ~55**, a 2.5x faster repeat rate and the most visible change in this wave. Also ~120-215 ms off every OK / Cancel / Save / Don't Save (`at_mdclose`, atui.inc:1142): `at_mdrect` caps a card at 380 wide centred, so the right edge is 552 on Hercules against `at_sbx` 680 — **no shipped alert reaches the gutter on any adapter**, and that call can simply be gated.
- Bytes: **+120 to +260 .text, +6 bss** (the bss is instance-local, `AT_BSS_TOTAL`, artful.asm:941).
- Hazards: **`at_fs_paint_all` (artful.asm:342-357) is a missed invalidation and it is a shipped-defect-shaped one** — it whitens the whole screen and then calls `at_sbar` through `at_fs_paint_body`, and `at_ondlg`'s `.save` path (atfile.inc:563) reaches it with the document and view unchanged, so the banked dispatcher returns 0 calls and the bar is erased and never redrawn. It must poison. **W_PAINT arrives with a WM damage region armed**, so setting the bank after a clipped paint records a bar that may never have reached the glass — **poison** on the way out of `at_paint`, do not set. `[at_sbshown] = 0` must mean *poisoned*, not *hidden*, because bss is loader-zeroed. The vacated strip is `sbx+1 .. sbx+14` (14 wide, matching the shaft at :1512) — the thumb is 16 wide, so the two frame columns need putting back or the bar grows notches; clamp the strip to `ty0+15 .. ty1-16` or a stale `[at_sbty]` puts a `FILL_GRAY` over an arrow box. `at_thumby`'s second caller (`at_sb_click`, atedit.inc:1165) changes with its contract. `gfx_fill_gray`'s phase is a function of absolute y (kernel/vga12.inc:4264), so a partial re-grey is phase-correct by construction — but A/B it on Hercules and CGA anyway, where a mis-phase is a stripe.
- SPEC.md: a new subsection under §46.1 (the performance contract, which already specifies the scroll path and omits the bar) stating that the bar carries banked state and what invalidates it. §13.10.6's table records Artful as the one deliberate `OS88UI_SCROLL` exception — "*no, and correctly*" — so that row gains the note that the private bar now has the shared bar's incremental behaviour. **Do not migrate to `OS88UI_SCROLL`**: that is +351 net bytes and contradicts a settled design decision (see part 6).

**Measurement:** W0.3's census on a View toggle (104 → 52), on a 10-line scroll (`at_draw_line` 29 → 10), and on an arrow-repeat sample (`at_sbar` far calls 21 → 0/5). W0.6's `changed` span for the double flash on the toggle.

---

### WAVE 3 — The compose: after Wave 1 it is the whole line
*Depends on Wave 1 only for its share, not for correctness. Set 64's own finding — "the emit is nearly free and the compose is the whole bill" — is exactly what Wave 1 makes true here.*

**Proposals:** S18 (CONFIRMED) as the headline, S11 (CONFIRMED) for the general path, S26 (PLAUSIBLE) as a ten-byte rider.

**3a — a scale-1, style-0 straight-line row emitter in `at_glyph`.** (S18) Gate after `xor bp,bp` (atrend.inc:737 — **not** at :722 as proposed, where SI and BX do not yet exist): `or dl,dl / jnz .general / cmp word [at_psc],1 / jne .general` (word, not byte — `at_psc` is a word and a low-byte compare is a silent wrong render), then eight unrolled `{ lodsb ; and/mov [bx],al ; add bx,AT_S1ST }`. Keep the whole existing body as `.general` — it is the path headings, zoom 1, every styled cell and `at_bigtext` take.
- Win: ~4,230 → ~869 clocks a cell, **887 → 182 µs**, 4.9x. Hercules line: at_compose 53 → **14.8 ms**. Standalone that is ~1.5x on a line; after Wave 1 it is the difference between a 61 ms line and a **23 ms** one.
- Bytes: +66 to +77 .text.
- Hazards: **standalone the body must be `mov [bx],al`, not `not`/`and`** — the `and`-of-complement form is correct only under Wave 1's polarity, and `mov` is both cheaper and safe (`at_compose` clears exactly `[at_prh]` rows at :578-585, and no two visible cells share a byte column at scale 1). `lodsb` introduces a DF dependency the routine does not have today — either document the precondition or spend 2 bytes a row on `mov al,[si] / inc si`. Note **an H4 takes the fast path** (`at_cellwtab` level 3 = 8 = scale 1 with rowh 12) and **Markdown raw mode is 100% fast path**; the pixel gate must cover both, plus scale 2 (every H1/H2, and all body text at zoom 1) and all five `AT_ST_*` bits.

**3b — hold the scaled row in AX:DX, not in `at_grow`.** (S11) `at_grow` is 4 bss bytes written and read back inside one row iteration — 8 memory accesses to move one byte of glyph. Put it in AH:AL:DH:DL, make the italic shear `clc / rcr ax,1 / rcr dx,1` (a genuine 32-bit shift through carry, 6 bytes and 6 clocks against 16 and 84), and `.vrep` becomes four `or [bx+n],r8`. **This is the only item that also helps scale 2 and 3** — every body cell at zoom 1 and every heading at zoom 0 — which 3a does not touch at all.
- Win: ~1,000 clocks/cell at scale 1 (subsumed by 3a) and ~1,540 at scale 2, ~29% off `at_glyph` on the general path.
- Bytes: **−31 .text, −4 bss** (measured by assembling both bodies: 203 → 172).
- Hazards: `loop` becomes illegal once CH and CL share CX (`dec`/`inc` preserve CF, so the `rcr` chain is safe across it); initialise as `mov ch,8`, never `mov cx,8`. **Byte order is load-bearing and silent when wrong** (AH = leftmost 8 px). The italic row test inverts: `cmp bp,4 / jae` counting up becomes `cmp ch,5 / jb` counting down — the naive transcription shears the bottom half and mirrors every italic. `.s2` must do the **low** nibble first once AH is the source.

**3c — do not compose a blank glyph.** (S26) Ten bytes at `at_glyph`'s entry, before the seven pushes: `cmp al,' ' / jne .draw / test dl,AT_ST_L|AT_ST_S / jz .skipret`. Space is the only character in 32..126 whose eight `at_fontbuf` rows are all zero, and bold, both nibble doublers (`at_dbl[0]`/`at_trp[0]` are provably 0) and the `clc`-seeded shear all preserve that; `AT_ST_L` and `AT_ST_S` are drawn by `at_ruleat` *after* the row loop and are the only inkers of a blank cell.
- Win: ~1 space in 5.7 of English prose, so ~11 of 74 Hercules cells → **~8 ms off a 47.7 ms compose, 16.4%**, for 10 bytes. Compose 14.8 → ~12.4 ms after 3a.
- Hazard: this is a **font-content dependency with no guard**. `make FONT=<name>` bakes any `fonts/*.f8`; a face that inks glyph 32 renders blank spaces silently. Name `at_fontbuf[0..7]` explicitly in the comment and add a host-side assertion in `tools/os88font.py` that char 32 is eight zero bytes.
- **Its companion `.vrep` zero-OR elision is REFUTED — do not build it.** The premise ignores that the italic shear runs *after* the `at_grow+1/+2` zero stores and rotates bit 0 into `at_grow+1` bit 7, so a bold-italic scale-1 `M`/`W`/`m`/`w`/`~` silently loses its rightmost sheared pixel column. 3b subsumes the win anyway.

**SPEC.md first.** §46.4's "*styling ROM 8×8 glyphs itself — bold is an overstrike, italic a two-step row shear, headings bit-doubled/tripled scale-ups*" gains the statement that the unstyled scale-1 cell — nearly every body character — has a straight-line emitter and does none of that dispatch, that the general body is the contract for everything else, and that the fast path's **preconditions** (`at_compose`'s whole-strip clear, the non-overlap of cells) are load-bearing rather than incidental once the `[bx+1..3]` stores are gone. Add the blank-cell skip and its font precondition. `at_glyph`'s own header (atrend.inc:705-710) changes too.

**Measurement:** bracket `at_glyph` under MartyPC before and after, per scale and per style bit. Pixel identity across three adapters, scales 1/2/3, each of the six `AT_ST_*` bits, and specifically an **italic scale-3 glyph whose overhang lands in strip byte 3** — the only case that exercises the DL end of the shear.

---

### WAVE 4 — What the line-count change and the fallback path cost

**Proposals:** S1/S19b (PLAUSIBLE, one mechanism), S9 (PLAUSIBLE), S13 (CONFIRMED, kern_small only).

**4a — scroll the band on a line-count change, do not repaint everything below.** (S1)
`at_apply_edit`'s `.below` arm calls `at_redraw_below`: one `SET_COLOR` + one full-width `GFX_FILL` from `[at_rby]` to `[at_ty1]`, then one `at_draw_line` per visible line to the bottom. When the changed span is smaller than the band below it, the right primitive is one `OSAPI_GFX_SCROLL` of that band plus a redraw of the changed span and of whatever the blit could not describe. `at_scroll_to` (atrend.inc:1264-1338) is the working precedent **in the same file**, including its CF fallback and its `.bb`/`.upband` band loops.
- Win: Hercules, Return at line 14 of 28: ~1,135 → **~310 ms** whole-keystroke, or 1,015 → 192 ms counting only what this owns — **3.7x**. VGA 6.6x. CGA 2.2x (the 16-line view makes the band below short, and that is where the win is weakest — test it there). After Waves 1+3 the ratio holds but the absolute falls to ~230 → ~90 ms.
- Bytes: **+230 to +300 .text, +4 bss**. Nothing is deleted — `at_redraw_below` survives as the fallback.
- **The hazard that decides whether this is buildable at all: the dy anchor cannot be derived from the new table.** `at_relayout` has already run and `at_splice` has overwritten the old `at_lattr` entries when `.below` is reached, so the old heights of lines F..J−1 are gone. The proposed gate — "every moved line has the same `at_lgeom` rowh" — is evaluated on the **new** table and misses the case that matters: Writer is the default (artful.asm:138), `at_rowhtab` is `10,20,20,12`, and backspacing a `# Head` line onto the body paragraph above leaves every new line at rowh 10 while the old span held a 20. dy computes as −10 against a truth of −20 and ten rows of the document are silently duplicated. **The fix is cheap and must be in the design:** inside `at_relayout`, immediately before `call at_splice` (atdoc.inc:1064), call `at_line_y` with BX = `[at_rlj]` while the table is still old and bank the answer in a word; then dy = new_y(F+K) − old_y(J) exactly, and no uniformity gate is needed. Handle CF=1 (J off-screen, or the `.tailend` path where `at_rlj` = `at_nlines`) as "nothing survives below": fill, do not scroll.
- Three more that the scouted version got backwards: **the changed span is K lines, not one** (`[at_dfrom]` is the containing logical line's first visual line and `[at_rlk]` the staged count — reuse `at_redraw_range`, and run it **after** the scroll); **content moves UP on a delete**, so the bottom |dy| rows are vacated (nothing is revealed at the bottom on an *insert*, which is the reverse of the proposal) and the delete arm needs `at_scroll_to`'s `.bb` loop **plus** a residue `GFX_FILL` when the document ends inside the view; and **`at_draw_line` clips itself at ty1**, so scrolling up moves already-clipped pixels into a slot that now wants a full row height.
- Two of the scouted hazards are false and must not drive the design: W_ONKEY runs under the gfx lock with **no clip region armed** (kernel/ui.inc:121-141), so `wm_clip_test` will not refuse; and every caller of `at_apply_edit` collapses `at_sela`/`at_selb` to the caret first, so `at_selxor` is a no-op on this path.
- **The commonest edit is not helped.** Typing at the end of a document puts `at_dfrom` on the last visible line, so `at_redraw_below` is already cheap and `at_seecaret` then scrolls again — this change adds a wasted ~52 ms scroll there. Gate on K < lines-below, and confirm no regression at the bottom of the document.
- SPEC.md §46.1's scroll sentence must admit what an ordinary Enter costs today and when the scroll is refused; §46.4's "one line, one blit" heading gains a second primitive.

**4b — blit the line's ink, behind a per-row watermark.** (S9)
`at_parse` already publishes the pen's end at `at_xmap[at_pcnt]`. Keep a 42-word array indexed by (line − `[at_top]`) of the width that row was last blitted at; set `[at_xw]` and CX to `max(ink + 8, watermark)` and store the new ink.
- **Scope, stated honestly: after Wave 1 this is a kern_small and colour-code-span change on kern_big.** On kern_big/1bpp the blit is a band and `at_expand` is gone. It is worth ~37-47 ms a line **on kern_small**, where `gfx_blit1` is `stc`/`ret` and the 4bpp path is the only path — the machine least able to fund the traffic — and it is worth `at_expand`'s ~20 ms unconditionally on a colour adapter.
- Bytes: **+115 to +140 .text, +96 bss** per instance.
- Hazards: **CX = 0 is catastrophic** — `at_expand` does `shr ax,3 / mov cx,ax / loop`, so a blank line gives 65,536 iterations writing 256 KB forward through the package bss; clamp width to ≥ 8 (`at_bigtext` at atui.inc:1286 already does exactly this and is the precedent). **Italic bleeds one byte past the pen** (`at_glyph` ORs four bytes and the shear can land a bit one column on), so use `at_xmap[at_pcnt] + 8`. **Poison rule (a) is wrong at `at_mclose` and `at_mdclose`** — both fill a *partial* rect and redraw only the rows the box covered, so zeroing every entry there strands the tail of every row *below* the box; those two sites need **no poison at all**, because `max(ink, watermark)` already repaints anything the box whitened. `at_redraw_below` must poison only from `(BX − [at_top])` down. Bounds-check the index — it is derived from document state and lands as a word store into a 42-entry array. The failure mode is a **plausible picture**, so this needs the driven gate: type a long line, delete back to short, compare pixels right of the caret against the same sequence with the watermark forced to `[at_tw]`.

**4c — a grey-free `at_expand` row loop.** (S13) One gate byte `[at_bggray]` (set by `at_codecols`, cleared at all **five** `at_cellbg` wipe sites — atrend.inc:132, :433, :689 and atui.inc:1266, the last two omitted by the proposal) selects a straight-line loop: `mov al,[bx] / inc bx / mov si,ax / shl si,1 / shl si,1 / add si,at_x4w / movsw / movsw / loop`, dropping the per-byte `at_cellbg` probe, the `or ah,ah`, the `jnz` and a `jmp` to the next instruction; hoist `[at_xw]>>3` out of `.row`, where it is a per-**call** constant recomputed on every row.
- Win: 154 → 94 clocks a source byte, **~60 clocks/byte**, Hercules 23.9 → 14.6 ms a line. **Same scope caveat as 4b: kern_small and colour only, after Wave 1.**
- Bytes: **+60 to +85 .text, +1 bss**.
- Hazards: SI cannot stay the strip walker (`movsw` needs it for the table), so this is a whole second **row** loop with BX-based striding and BP holding the hoisted count; BX and DI need per-row **fixup**, not accumulation (BX advances `at_xw/8` against `AT_S1ST`=80, DI `at_xw/2` against `AT_S4ST`=304). AH must be 0 for the whole loop — comment the constraint, because any later edit that writes AX indexes 1 KB past `at_x4w`. Do **not** substitute the zero-bss gate `cmp [at_pcb0],[at_pcb1]`: `at_codebg`'s `.clear` wipes `at_cellbg` and leaves those set, which would send exactly the 1bpp code lines down the slow loop.
- SPEC.md §46.4.1's closing sentence about the expander's hot loop paying nothing should name the gate.

---

### WAVE 5 — The layout walks
*Per keystroke rather than per line, so its share rises as Waves 1 and 3 land. Order inside the wave matters: take 5a before 5b (deleting `.findnl` shrinks nothing 5b measures; deleting `at_respan` first makes 5a's model look larger than it is).*

**5a — delete `at_relayout`'s `.findnl` walk.** (S7, CONFIRMED) It reads the whole paragraph through `at_getb` only to compute CX = one past the newline, which `at_scan` rediscovers at `.endline` (atdoc.inc:775, which **already** does `inc si / mov [at_scpos],si`) and `.enddoc` (:779, which already stores the same value). Add a one-paragraph flag; `at_relayout` passes CX = `at_dlen` once and reads `[at_scpos]` back as DX.
- Win: 400 chars × ~210 clocks = **17.6 ms** off every keystroke in a 400-char paragraph, linear in paragraph length.
- Bytes: **−4 .text**, +0 bss (`at_dfromb`, artful.asm:994, is a genuinely free pad byte — but only **one** proposal may claim it).
- **Hazard: the proposal's named gate is a hang.** Testing `[at_lput] > 0` at `.lline` fires on entry for every paragraph after the first, because `at_relayout` stages successive paragraphs into the same window — `at_scan` would return having emitted nothing with `[at_scpos]` unchanged and `.para` would spin forever. Put the test at **`.endline` after the store**, branching to `.done` (which is where `pop cx` lives — `at_scan` documents that it preserves CX). Make the flag **self-clearing** at `.done`: left set across `at_relayout`'s exit, `.fallback` calls `at_layout` with it still set and the full layout stops after one paragraph — the document silently loses every line past the first, on exactly the bulk-edit path nobody exercises per keystroke.
- SPEC.md: the section that describes this is **§46.3** ("*rescans exactly the edited paragraph(s)*"), not §46.9, and it stays true. Check §46.1's "one-paragraph relayout" wording instead. **Do not manufacture a SPEC edit.**

**5b — delete `at_respan`.** (S8, CONFIRMED) It exists only to answer "what was DL at the break point", and both break paths already know. Hard break: bank DL before `call at_span` (atdoc.inc:756) — better, **swap the order** to `cmp di,[at_sccw] / jb .wrap / call at_span`, so DL at `.wrap` is already the pre-span state and the per-character bank disappears entirely. Soft break: a space toggles nothing, so DL at `mov bp,si` (:762) *is* the state entering bp+1.
- Win: 5 breaks × 74 chars × ~366 clocks = **28 ms** off a 400-char keystroke, ~33% of the layout.
- Bytes: **−34 .text, −1 bss** (`at_sclst`'s word becomes dead).
- Hazards: **the proposed restore is dead code** — `mov dl,ch` at `.hard` is overwritten by `.vline`'s `mov dl,[at_scspan]` at :748, and `at_emit` reads `[at_scspan]` before that; the restore must be `mov [at_scspan],<banked>`. `.hard` is a **shared fallthrough** (the soft path falls into it), so the soft reload must sit between :770 and :771 with a single store at `.hard` serving both. **Leave `[at_scskip]` alone on both paths** — the scouted version resets it to 0FFFFh, which is wrong on a hard break landing on the second character of a `**`/`~~` pair, and it is provably unnecessary on the soft path (`at_scskip` is always marker+1 and bp holds a space).
- No SPEC edit: `respan` appears nowhere in SPEC.md. Optionally add one sentence to §46.3 that the nibble is banked at the break.

**5c — inline `at_span`'s delimiter reject.** (S14, CONFIRMED) Three immediate compares (`'*'`, `` '`' ``, `'~'`) at the two scanning sites (atdoc.inc:756, :802), keeping the `[at_scskip]` test inside `at_span` (it is only ever set to the position of a `*` or `~`, so it always reaches the call). **Explicitly not a 256-byte table**: BX is live at both sites and `cmp byte [bx+disp16],0` is 19 clocks against `cmp al,imm8`'s 4 — the table is the shorter encoding and the compare chain is still cheaper, which is this lens running in reverse.
- Win: ~65 clocks a non-delimiter character. **10.5 ms** off a 400-char keystroke; **~1.7 s** off a full `at_layout` of a maximal document (the View/Zoom toggle path).
- Bytes: +24 to +28 .text.
- Hazard: the delimiter set is now in four places and **already diverges deliberately** — `at_parse`'s FSM tests those three **plus `[`** for links, which `at_span` must never gain. Mirror `at_span`, not `at_parse`. Use `%%`-local macro labels. Place `.dlm: call at_span / jmp short .nodlm` out of line so the common path takes three not-taken `je`s.

**5d — inline `at_getb`'s fetch in `at_scan`'s `.ch`.** (S12, PLAUSIBLE) Hoist `mov es,[at_dseg]` and the gap bias out of the per-byte loop.
- Win: ~95-100 clocks a character. **~24 ms** off a keystroke; **~2.5 s** off a full layout.
- Bytes: ~+58 .text, +2 bss.
- **Hazards, and this one is wrong as scouted in three places.** The inlined fetch must leave **BX = the LOGICAL offset** — `at_span` reads `cmp bx,[at_scskip]` and writes `mov [at_scskip],bx`, and `at_peek` re-derives from it; today `at_getb` *preserves* BX, which is what makes `mov bx,si` at :750 sufficient. A biased BX breaks `**`/`~~` parsing only past the gap, i.e. only after the caret, producing wrong styling rather than a fault. **CX is live at `.findnl`** (document length, then the loop bound, then the required `at_scan` argument) — use DX or BP there. The bias must be computed **after** `.haveh`'s heading counter finishes with CX. And artful.asm:534-543 cites `at_getb`'s per-**character** segment reload as the justification for the app's `OSAPI_MEM_PARKSAFE` declaration — that comment must be replaced with the real argument (only the UI task claims, §66.3 context 3; nothing in `at_scan` yields), or the next author reads a justification the code no longer satisfies. The invariant this creates is exactly the one a 2.5 s full layout invites breaking: the first person who adds a progress cue or a yield inside `at_scan` reads the document through a stale ES with no fault.
- **SPEC.md §46.9's "~32 clocks a character", its "~2 ms paragraph rescan" and its "a fifth of a second on a maximal relayout" are all wrong by ~4-5x and must be corrected BEFORE this lands**, along with PERFORMANCE.md:459's row. §46.1's "*caret motion never moves the gap (at_getb branches around it)*" needs the branch's new home named.

**5e — carry the row y down the draw loops.** (S6/S15, CONFIRMED) `at_line_y` walks from `[at_top]` calling `at_lgeom` per step, and five loops call it once per line, so drawing N lines makes N(N+1)/2 `at_lgeom` calls to compute N numbers that differ by one row height.
- Win: a full VGA repaint drops ~54 ms of pure arithmetic; **the valuable half is `at_scroll_to`'s `.bb`**, which walks *every* visible line to find the 1-2 the blit vacated — the same 27 ms (Hercules) / 55 ms (VGA) spent per one-line scroll step, ~35-47% of a scroll after Waves 1-3.
- Bytes: +24 to +30 using `add ax,[at_prh]` in the three *draw* loops (`at_parse` already sets it and `at_draw_line` preserves it), +40 to +50 with an `at_lgeom` call.
- Hazards: **`.bb` must keep `at_lgeom`** — `.bskip` bypasses the draw, so `[at_prh]` is stale there, and "advance after each `at_draw_line`" leaves skipped lines at the same y, which is an infinite mis-draw in the loop with the biggest win. **DX is live in `at_redraw_range`** as the range end and `at_lgeom` returns rowh in DX. Each loop must own the `y >= [at_ty1]` exit `at_line_y`'s CF used to provide, reading `[at_ty1]` from memory (44/460, 44/328, 30/196 — a constant is wrong on two adapters of three). Hoist the seeding `at_line_y` **below** `at_redraw_range`'s existing clamp and bound tests — `at_nav_paint` can reach it with BX = 0FFFFh. `at_redraw_below`'s seed is **free**: `[at_rby]` already holds it. `.ub` wins nothing (it exits after ≤3 lines) — do not convert it, and if Wave 2b raises the threshold, write the dependency down.
- No SPEC edit — which also means there is no written contract to check it against, so it goes in behind the same pixel-identity gate as the rest.

**5f — resolve `(zoom, writer)` into a small table, or fast-path the uniform-height case.** (S16/S10, both PLAUSIBLE, take **one**) `at_lgeom` re-derives `zoom*4 + level` and re-tests `[at_writer]` on every call: 262 → 135 clocks called, → 71 inlined in a walk. Worth ~1.1-1.6 ms per `at_maxtop` walk, twice per `at_sbar`.
- **Its stated invalidation rule is wrong three times and would ship a stale table**: `at_refit` never calls `at_geom_init` (which has exactly two callers, artful.asm:135 and :514), so resolving there misses Zoom In/Out/Default entirely; resolving "at the top of `at_setview`" reads the mode before it is stored (atcmd.inc:547); and artful.asm:135 runs before :138 sets `at_writer = 1`, so a launch-time resolve captures Markdown for a Writer document. **Resolve at the top of `at_refit`** — all four mutators funnel through it — plus once at launch after :138. `at_lgeom` reads no `vid_*` state at all, so the "display geometry feeds the answer" premise is false, and that false dependency is what anchored the hook to the wrong routine.
- S10's alternative (Markdown mode has one row height, so replace the walks with arithmetic) is **zero in the shipped default view** (Writer, artful.asm:138) and drops `at_xy2log`'s ty1 clamp and `at_maxtop`'s upper clamp as written. Prefer S16, and price the 90%-variant (cache `zoom*4` in one byte, ~11 bytes) before committing ~110.
- Bytes: ~+90 to +125 .text, +16 bss (per instance).
- SPEC.md §46.3 gains the invalidation set, naming **`at_refit`** as the resolution point.

**Measurement for the whole wave:** guest cycles between two breakpoints in `at_apply_edit`, at paragraph lengths of 100 / 400 / 800 characters and at document sizes of 1 KB / 20 KB, plus `bp_count` on `at_getb` and `at_span`. And a **styling gate**: type `**bold**`, `*i*`, `` `code` `` and `~~s~~` into a wrapping paragraph and assert the attr nibble — 5b and 5c both fail as *silent wrong styling*, and specifically a `**` pair straddling a hard (no-space) wrap.

---

### WAVE 6 — Speculative, and only after 1-5 are measured
Each of these is independently arguable and none is a prerequisite for anything above. Take them in this order and stop when the measured keystroke is under a typematic period.

- **S25, the plain-line bit** — see part 4.
- **S23, `at_append`** — see part 4.
- **S21, pace the gesture loops.** `at_click_text` (atedit.inc:1013) calls `at_xy2log` — which contains a full `at_parse` — **one instruction before** the test that asks whether the pointer moved, so a held motionless button re-parses a whole visual line 170-345 times a second. Hoisting the raw-(x,y) skip above it is unambiguously right and ~28 bytes. **Pacing with `at_linger` is not**, and the blocker is written down in the kernel: kernel/ui.inc:1313-1317 says a level-polled loop paced to 18.2 Hz tracks the button by **edge, not level**, so a release-plus-re-press inside one tick is invisible — the drag never ends, the menu never dispatches — and §13.10.6.1 records that Artful takes no §13.7 edges, so there is no `evq_mup` for a package to call. Settle that before pacing. **And claim no CPU saving**: `at_linger` is a yield spin, `sch_switch` skips `[sch_idleslot]`, so a ready `ui_task` is re-picked and nothing reaches `hlt`. The honest statement is "the work a motionless held button does falls ~135x and the lock-pair rate ~10x".
- **S27's Tab arm only.** `at_ed_key`'s Tab (atedit.inc:79-85) loops `at_type` four times and pays four complete keystroke cycles — relayout, splice, full paragraph repaint — for one Tab. One `at_insn` of four spaces and one `at_apply_edit` is **4.0x** on a key that is one edit by any reading, and it needs none of the BIOS-buffer machinery. The **drain half is refuted** (see part 5).
- **S13/S9 on kern_small** if Wave 4 deferred them.
- **S24, the class-table string scan** — see part 4.

---

## 4. THE INNOVATIVE ONES

Four proposals are genuinely new here rather than lifted from Note Pad or TeXPad. Note Pad's contributions to this plan are its *row signature* (which no survivor uses — all three attempts to narrow the redraw range were refuted, part 5), `np_append`, `np_sbcheck`'s banked bar and `np_selpace`; TeXPad's are `tp_edmark`'s measure-the-reach-before-the-edit (Wave 4a) and its raised scroll threshold (Wave 2b).

**S25 — spend the free bit 3 of `at_lattr` on a PLAIN-LINE flag, computed where the bytes are already being read.** `at_lattr` carries heading level in bits 0-1, continuation in bit 2 and the span nibble in bits 4-7; **bit 3 is unused and every reader masks it off** (verified: `at_lgeom`'s `and ax,3`, `at_parse`'s `and ah,0F0h`/`and dx,3`/`test al,4`, `at_lhome`'s `test byte,4`). Define it as: heading level 0, entered with a zero nibble, and no ` * ~ [ ` anywhere the scan walked. `at_scan` already reads every byte, so the flag costs 4 bytes of `at_span`'s hot path and one call per visual line. Three consumers read it: `at_parse` takes its existing `.raw` loop even in Writer mode (the styled FSM's answer for such a line is *provably* `at_vis[]=0, at_sty[]=0, at_xmap[i]=i*at_pcw`, and `at_reveal` is a proven no-op); `at_caret_on` stops calling `at_parse` at all (`at_ccx = at_tx0 + (caret − lstart)*pcw`); and Wave 6's `at_append` gate becomes one `test byte [bx+at_lattr],8`.

**The arithmetic supports it, and it is a proof rather than a hash — which is what makes it different from Note Pad's signature: there is no cache and no invalidation rule to get wrong.** `at_parse`'s styled `.sloop` is 115 instruction bytes on a plain letter against `.rloop`'s 29 → 373 clocks a character at the 8088 fetch floor. Per Hercules line, 74 × 373 = 27,600 cycles = **5.8 ms** saved; `at_caret_on`'s whole parse (8.1 ms on Hercules) disappears on every arrow key **and on every lit blink phase** — 1.011 lit phases a second is **0.150% of a 4.77 MHz 8088 given back at idle**, and that claim stands as written. Against a 122 ms line today it is 5%; against Wave 1+3's ~21 ms line it is **28%**, which is why it belongs after them and not before. Bytes: **+150 to +195 .text, +1 bss**. Its one defect: the clear must also be at `at_span`'s `.skip` arm (atdoc.inc:842), which returns with no toggle run — after a hard break `at_respan` can leave `[at_scskip]` on the next line's first character, and a `*` there is skipped, plain survives, and `at_parse` (which resets `at_scskip` per line) would have hidden it. Five bytes, non-optional. SPEC.md §46.3 must **publish** bit 3 and the predicate exactly: it is a contract between the layout and the renderer and the two can disagree silently.

**S26 — do not compose a blank glyph.** Ten bytes, 16.4% of `at_compose`, riding on the single fact that space is the only printable whose eight font rows are all zero and that the two styles which ink a blank cell are drawn *after* the row loop. Arithmetic supports it (part 3, wave 3c). Its risk is a font dependency with no guard, which is cheap to add.

**S24 — lay out a boring run with string instructions.** A 256-byte character-class table plus `es lodsb / xlat / or al,al / jnz`, unrolled four times, entering the per-character interpreter only at the ~1-in-30 characters that can change anything; the backward space search becomes `std / repne scasb`. Nothing in Note Pad or TeXPad does this. **The arithmetic supports it but not at the claimed ratio**: with `at_respan` still in place it is ~1.9x on the layout, not 7.2x, and reaching 7.8x requires converting `at_respan` too — which Wave 5b **deletes**, so the two conflict and 5b is the cheaper of the pair. Its class set needs only four entries (`[` buys nothing — `at_span` never sees links), it must split at the gap in **three** places (the forward run, the backward space scan, and `.findnl`, which 5a deletes), and one of its sub-items is an outright defect: inlining the probe in `at_lrange` as `mov es,[at_dseg] / mov al,[es:bx]` clobbers ES, and `at_slice` sets ES=DS before calling it and then `rep movsb`s to ES:DI — the line slice would be written **into the document**, which is verbatim the failure §46.9 names as the reason `at_getb` preserves ES. **Verdict: take Wave 5 first; revisit only if the measured layout share is still large.**

**S28 — block the line table** (`at_lstart` entries relative to a per-32-line base). Genuinely new, and it attacks the one true O(document) item on the typing path. **The arithmetic does not support it at realistic document sizes.** At the 2,047-line ceiling it saves ~28 ms a keystroke; at 1,000 lines ~5.9 ms; at 200 lines it is a **wash** once the ~0.8 ms/keystroke reader tax on `at_line_of`'s four calls is paid; and it saves **zero** on any keystroke that changes the line count (every Enter, every wrap), because the tail move stays linear either way. +315 bytes and a derived cache whose invalidation rule the proposal denies having. Refuse it unless a field document turns out to be thousands of lines.

**S23 — `at_append`** is *adapted* from Note Pad's `np_append` rather than new, and its ArtfulType form has a real defect: the strip stamp guards `at_strip1` but the fast path also consumes the **live parse arrays**, and eight `at_parse` call sites compose nothing, so the two have different lifetimes. It needs a second stamp written by `at_parse` itself. Its arithmetic is also 1.6-2.5x optimistic because it prices an 8×10 blit per pixel and quotes 756 µs as an arrival — the measured 1bpp figure is ~2,863 cycles a **row** whatever the width (so ~6.0 ms, not 1.3), and on VGA an 8 px block is refused the planar decoder outright (`cmp cx,64 / jb .pdone`) and pays the run writer. Corrected it is **6x to 13x**, not 20-32x — still worth having, after Waves 1 and 3 have made the residual something other than the blit floor it cannot reduce.

---

## 5. WHAT DID NOT SURVIVE

- **K1 — repaint only the staged lines whose `(start, attr)` differ, keyed on `at_stgs[i+1] <= edit offset`.** Refuted by a constructible counterexample from `at_scan`'s own wrap code: deleting the break space makes the new line *shorter* while start, attr and the `<=` test all still hold, so the walk skips a line whose content shrank and stale text stays on the glass.
- **K2 — replace `at_linger`'s tick spin with one `OSAPI_TASK_SLEEP`.** Not the same wait: both sites run on the **UI task**, and `sch_wake_ui` writes `T_STATE=1` on task 0 from every mouse packet, keystroke and queued event — so the three-flash Mac idiom becomes a blink whenever the hand moves and the arrow auto-repeat becomes mouse-jitter-paced. This is why the kernel's own three lingers on that task are `task_yield`+`[ticks]` loops.
- **K3 — cache `at_mcell`'s O(n²) `OSAPI_FONT_WIDTH` walk to make the menu loop track twice as finely.** The mechanism is real and the win has no consumer: the mouse is 1200 baud 7N1, so the position latch changes ~44 times a second and the loop already oversamples it 10-15x. What is left is an ordinary 1-5% cleanup on cold paths, which PERFORMANCE.md:9625 already prices at "under 5% of the worst case".
- **K4 — start the repaint at the visual line containing the edit, derived from the edit offset.** The stated invalidation rule reasons only about the new layout, but greedy word wrap means a line ending *before* the edit can have its break decided by text *at or after* it: insert one character mid-word near a wrap boundary and a whole word is left stale.
- **K5 — replace `mov cl,n / shl reg,cl` with repeated `shl reg,1`.** Refuted by this repo's own measured instruction law — `max(exec clocks, 4.34 × bytes)`, and `shl r16,1` measures 8.69 clocks, not 2. Every named site has n ≥ 3, where the trade **inverts**: the n=4 site goes 28 → 34.7 clocks and the `.s3` block 52 → 69.4. PERFORMANCE.md:9129 had already refuted this substitution class once.
- **K6 — repaint only the staged lines that changed, on `(start, attr)` equality against the live table.** Compares different coordinate systems (staging holds new offsets, `at_lstart` still holds old ones), and `at_relayout`'s own comment says old == new at or before the edit point — so typing at the end of a paragraph makes every staged entry compare equal and **the typed character is never drawn**. Its sub-items survive: `at_draw_cline` (atrend.inc:1158) really is 25 bytes of dead code that calls itself "the keystroke path" and should be deleted, and the double `at_sbar` is Wave 2.
- **K7 — pre-take the undo snapshot in the idle gap on the blink worker.** The reservation is not where it thinks (`at_snap_push` writes after its eviction loop, and eviction is the steady state at `AT_UMAX`=15 / 24,576 bytes), its headline is measured at the one document size where `at_sndrop` costs nothing, and the warm-up window (2.02 wakes/s → ~5 s for 20 KB) is longer than the gap it targets — "click then type", the case named, never reaches a warm snapshot.

**…and these load-bearing claims *inside* survivors were refuted; do not re-derive them.** The `.vrep` zero-OR elision (S26's companion) breaks bold-italic on five shipped glyphs. S24's `at_lrange` inlining writes the line slice into the document. S9's poison rule at `at_mclose`/`at_mdclose` strands every row below the box. S1's uniform-rowh gate is computed on the wrong table. S12's inlined fetch corrupts `at_span`'s BX. S7's `.lline` gate is an infinite loop. S16's three invalidation points all miss. S8's `at_scskip` reset breaks a `**` pair on a hard wrap. S21's "0.15% of the machine" — `at_linger` does not halt. S27's overrun premise — `kbd_ovflow` discards the **newest** key, and at 60 wpm on CGA the buffer never fills. S20's alternative of migrating to `OS88UI_SCROLL` is +351 net bytes and contradicts §13.10.6's settled "no — and correctly".

---

## 6. OPEN QUESTIONS

1. **Does kern_small get a `gfx_blit1` body?** §5.4.2.5 records one BUILT, MEASURED at **24x** and REFUSED at +419 bytes / one `.cold` rung. Wave 1 gives it a **second customer** whose cost is on the *keystroke* path rather than a repaint: without the body a small-machine ArtfulType keystroke pays 64 ms of arrival a line where kern_big pays 3.3. §39.27.4's standing decision is that kern_small bytes are **banked, not budgeted** and the small build may stay slower. This is a question for the repository owner, not a change to make.
2. **`ARTFUL.O88`'s image grows by ~500-900 bytes across these waves.** The package has ~22 KB of `APP_MAX_SIZE` headroom, but `apps360.img`'s cluster budget is the real constraint. Build and check all four geometries before the last wave lands.
3. **Should the Hercules body row height be 12 rather than 10?** §5.5.1's `gfx_scroll` fast path needs `dy & [vid_bmask]` = 0 — a multiple of 4 on Hercules — so a one- or three-line scroll takes the general path, ~7.5 ms extra over 274 rows. Changing `at_rowhtab` is a **look** decision (28 lines → 23 on Hercules), not a performance one. The honest outcome may be to record the miss.
4. **Is `at_rlk`-scoped repaint acceptable, or does §46.1 need a stronger promise?** Three independent attempts to narrow it were refuted. Waves 1+3 take a six-line-paragraph keystroke from ~730 ms to ~126 ms, which may be enough — but if it is not, the remaining lever is Wave 6's `at_append` plus S25, not another predicate over the staging window.
5. **Wave 6's pacing needs a §13.7 decision.** §13.10.6.1 records Artful as deliberately poll-shaped and taking no edge callbacks. Pacing a level-polled loop to 18.2 Hz is a correctness change the kernel already documents as dangerous. Either the app takes `OSAPI_WM_ONDRAG`/`ONMOUSEUP` (which are CF=1 on kern_small) or the loops stay unpaced and only the raw-position skip lands.
6. **Nothing here has been on iron.** Every wall-clock figure in this plan is PREDICTED from measured unit rates; the only measured end-to-end analogue is Paint's 809 → 36.6 ms. Wave 0 makes them MartyPC measurements, which agree with the 5150 to 0-4% on 45 of 47 `gfxbench` rows — but the **flicker** half (W0.6) cannot be taken on Hercules at all, and a field run on the 5150 (docs/FIELD-MACHINES.md) is what would settle the per-line figure and Wave 4a's scroll-vs-repaint break-even.
7. **PERFORMANCE.md and SPEC.md carry two stale rows about this app** that must be corrected regardless of which waves land: `at_getb` at "~32 clocks a character" (§46.9 and PERFORMANCE.md:459, understated ~4-5x, with §46.9's "~2 ms" and "a fifth of a second" derived from it), and PERFORMANCE.md:4310's listing of ArtfulType among packages that "call `OSAPI_FONT_STR` and never `FONT_RUN`" — §46.10 converted all eight sites and `grep` finds zero.
---

## 7. WHAT IMPLEMENTATION FOUND — and it moved the plan

Waves 1, 3a and 5a are BUILT (SPEC.md §46.4.2, §46.4.3, §46.3.1). Everything in
parts 1–3 above was PREDICTED; `tests/atkey.py` now measures, and three of the
plan's own conclusions did not survive contact.

**The keystroke, MEASURED.** Hercules, a three-visual-line paragraph,
`at_onkey` entry to its return, on a cycle-accurate 4.77 MHz 8088:

| | one keystroke | |
|---|---:|---|
| before any wave | 520.7 ms | |
| §46.4.2, the band emit | 329.0 ms | 1.58x |
| §46.4.3, the scale-1 composer | 154.1 ms | 2.14x again |
| §46.3.1, one walk per paragraph | 142.8 ms | 1.07x again |
| | | **3.65x in total** |

The band emit's per-line saving was predicted at 64.3 ms and measures **63.9**,
which is the closest thing to a validation the unit rates in part 1 have.

**`at_rlk` really is the paragraph.** The row reads it back: 3 for a three-line
paragraph. §46.1's "that paragraph's visual lines" is exactly what happens, and
part 5's three refuted attempts to narrow it stand.

**WAVE 2b IS REFUSED, and the reason generalises.** The page-sized scroll
threshold was built, drew 0 differing pixels on every scene, and **could not be
shown to save anything**: two scenes measured 1.00x and 0.95x. Its arithmetic
traded one `gfx_scroll` against N line repaints at ~122 ms a line — and waves 1
and 3 had just made a line ~30 ms, which collapses the trade. 15 bytes for a
win nobody can demonstrate is the trade CLAUDE.md refuses, so it was reverted.

**The general rule that falls out**: every proposal in this plan that trades a
`gfx_scroll` against line repaints — 2b and **4a** — was costed against a line
that no longer exists. Their predicted wins are upper bounds computed on a 122
ms line and must be re-derived on ~30 before either is built. That is the one
thing to know before picking this up again.

**And a measurement trap worth not repeating.** Both attempts to measure a
scroll failed to scroll at all — `[at_top]` moved by 0 — because a PageDown
from the top of the document only moves the CARET to the bottom of the view and
`at_seecaret` then finds it already visible. Bracketing that one measures a
caret move and calls it a scroll. A scroll scene has to spend a PageDown
unbracketed first, and must ASSERT that `[at_top]` moved.

**WAVE 2c IS BUILT** (SPEC.md §46.4.4) and measures **1.12x on an Enter** in a
document that fits — 71.5 → 63.9 ms on CGA, which is the full-height gutter
fill that used to run *above* the test deciding there was no bar at all. The
bar itself goes 21 far calls → 0 when nothing moved, 6 when only the thumb did.
Its value was NOT collapsed by waves 1 and 3, as predicted: the bar's cost is
scan-line setups, not line composition.

Two things it cost, both caught by `tests/atblit.py` and neither a crash:

- **`SI` is the arrow loops' own 1..5 counter.** Banking `maxtop` from `SI` at
  the thumb — thirty lines below where `at_maxtop` answered — banked **0**, so
  every later call missed its own bank, and handed `at_thumby_m` a zero
  divisor. It showed up as a thumb frozen at the top of the shaft while the
  view was at the bottom. `[at_sbmax]` is written at `.full`'s head now.
- **The windowed splash scene includes the kernel's desktop menu bar**, whose
  clock is a running one, so a whole-screen compare asks two boots to agree
  about the time. It passed until two runs straddled a minute and then
  reported 31 pixels at (624,6). The splash scene starts below `MBAR_H` now.

**And a bss trap worth a guard.** Adding `at_sc1` for §46.3.1 left `at_stgs`
saying `equ at_sclst + 2`, so the two aliased onto one address: `at_sc1` read
back as the staging window's first low byte and the feature was **inert for a
document starting at offset 0**, which is every test document. It was not a
crash and not a wrong picture — the flag simply degraded to "scan everything",
which is the old behaviour. `artful.asm` now carries a `%error` that fires on
exactly that shape, and it was verified to fire.

**WAVE 2a IS REFUSED, and the second attempt is the one that settles it.** The
first pass could not reach the tails at all and I wrongly concluded the harness
could not drive ArtfulType's own menu bar. It can: `os88ui.menu_pick` reads the
KERNEL's menu tables and so cannot see a fullscreen app's own bar, but that is
a convenience layer, not a limit — the mouse driver takes **absolute
coordinates**, so mirroring `at_mcell`'s geometry (the bar starts 8px in, each
title cell is `8*len + 16`) and `at_mitem_at`'s (`(y - 21) / AT_ITEMH`) drives
any of them with `at_menu_track`'s own press-drag-release idiom.

With Zoom In drivable, the answer is a **call count**, which is what
PERFORMANCE.md says a redraw is priced by:

    at_draw_text entries per ZOOM IN:  1 shipped,  1 with the hoist removed

**There is no double repaint.** The tails clamp `[at_top]` to `at_maxtop`
*before* drawing, and after a zoom that clamp already puts the view where the
caret is — so `at_seecaret` finds it visible and never scrolls. The same holds
for undo (which collapses the document) and for Style > None. The plan's
"104 far calls → 52, exactly halved" assumed the second repaint always happens;
it essentially never does.

The case that would double-draw needs the caret off-view after the command,
which needs the view scrolled **without the caret moving** — reachable only by
dragging the scroll bar or its arrows. That is a narrow enough case that 30
bytes and a new failure mode are not worth it, and the arithmetic for it would
have to be taken on that path rather than assumed.

**What was worth keeping is the harness.** The zoom scene stays in
`tests/atblit.py` as general coverage: the zoom/mode change is the only
command that moves the GEOMETRY every other wave's arithmetic is expressed in
— `at_lgeom`'s cell width and row height, and with them `at_maxtop`, the wrap,
the line table and the scroll bar's travel — and it asserts a zoom is
reversible, which nothing else did. It also proves a menu-driven path is
testable, which the rest of this app's commands need.

**WAVE 3b IS BUILT** (SPEC.md §46.4.5) and is the first change here that is
**both faster and smaller**: **1.06x** on a keystroke inside a heading
(238.3 → 225.2 ms, Hercules, three visual lines) for **−14 bytes**. Scale 1
body text measures **1.00x**, which is the design working: §46.4.3's
straight-line emitter never reaches this arm.

The win is smaller than the plan's "~29% off `at_glyph`" because that assumed
the FULL register plumbing including `.s2`/`.s3`. What was taken is the safe
subset — the row is hoisted into `AH:AL:DH:DL` once at `.sheared`, and the
shear and `.vrep` both work on registers, while `at_grow` stays as the staging
area the three scale arms write. That is where the memory traffic actually was:
`.vrep` re-read all four bytes **on every repeat**.

Who it helps is not who §46.4.3 helps: every heading, every styled span,
`at_bigtext`, and — because `at_cellwtab`'s zoom-1 body cell is 16px —
**every character of body text at zoom 1**.

**A scene-drift finding that invalidated earlier coverage.** The zoom scene's
`[at_top]` assertion fired on Hercules and VGA for *every* knob at once, and it
was right: the filler document was 22 lines, which overflows CGA's 16-line view
but FITS Hercules' 28 and VGA's 41. So on those two adapters the scrolled and
zoomed scenes had no scroll bar and nothing to scroll — they had been passing
while testing almost nothing. `FILLER` is per-adapter now, and the earlier
"0 differing pixels on herc and vga" for those two scenes should be read as
weaker than it looked; CGA was carrying the real coverage.

**WAVE 3c IS BUILT** (SPEC.md §46.4.6) and is the best of the three-series:
**1.12x** on a keystroke inside a heading (223.1 → 198.9 ms) and 1.03x on
scale-1 body text, for 49 bytes. It is larger at scale 2 exactly as the plan
predicted — a space on the general arm pays the whole scale/bold/italic
dispatch, where on §46.4.3's arm it pays eight fetch-complement-stores.

**The font dependency the plan flagged is real and is CHECKED, not asserted.**
`at_font_init` takes the kernel's own table through `OSAPI_FONT_GLYPHS`, and on
a `make FONT=` kernel that is a different typeface (§6.2) — which is the whole
reason this app stopped probing the ROM. So "glyph 32 is eight zero bytes" is a
fact about *that table*, tested once on the copy into `[at_blankok]`; a face
that inks its space composes spaces as before. Most of the 49 bytes are that
check, and it is the difference between a small optimisation and a silent
wrong render on a kernel nothing here builds by default.

The gate needed a change to be able to fail: **every styled run in its document
now contains a space**. A link's underline and a strike are the two things that
ink a blank cell, and `at_ruleat` draws them after the row loop — so a space
inside either must still be composed. `**bold**` and `~~struck~~` with no space
in them never tested that; dropping the `AT_ST_L | AT_ST_S` guard on purpose
now reads 16 differing pixels, which is exactly the rule segments under the
spaces.

**S25 IS BUILT — but as a LOCAL PROOF rather than as the flag it proposed**
(SPEC.md §46.4.7), and it is the biggest single win since §46.4.3: **1.18x** on
plain body text (141.3 → 119.3 ms, Hercules, three visual lines) and **1.00x**
on a heading, where the proof correctly refuses and the check costs nothing
measurable. 57 bytes.

S25's design was to spend `at_lattr`'s unused bit 3 on a plain-line flag
computed in `at_scan`, which already reads every byte. That is a contract
between the LAYOUT and the RENDERER that the two can disagree about silently,
and the plan already knew it needed clearing on a path `at_span` can leave by
(`.skip`). `at_parse` proves the line plain **where it is used** instead: one
pass over `at_lbuf`, which is already in memory and contiguous, at four
compares a character. That pays for itself several times over against
`.sloop`'s ~373 clocks a character, and there is nothing to invalidate.

The equivalence is provable, not likely: `.sloop` collapses onto `.rloop`
exactly when the line has no heading level, enters with a zero span nibble, and
holds none of `` ` `` `*` `~` `[` — the only four characters that reach
`.code`, `.star`, `.tilde` or `.bracket`. Two things that would spoil it do
not: `at_reveal` is a no-op when nothing is hidden (its first compare ends the
walk and `cmp cx, si / je .out` returns), and `[at_pcb0]`/`[at_pcb1]` are
initialised above the branch to the empty span `at_codebg` refuses on.

**The gate needed three new witnesses, and finding that out took breaking each
condition separately.** The first attempt left all three green: the italic
heading is excluded by its `*` anyway, so removing the heading test changed
nothing, and the wrapped bold run's closing `**` landed on the second visual
line, so the character test caught it before the entry-nibble test could. The
document now carries a heading with NO delimiter in it, and a bold run long
enough that its MIDDLE visual line has an open span and no delimiter at all.
Broken one at a time they read 1,716 / 396 / 910 differing pixels.

**What is left, re-ranked on the measurement rather than the prediction.**
**A profile of the keystroke that remains** says no single term dominates any
more. Per printable keystroke on Hercules in a three-line paragraph, MEASURED
call counts: `at_glyph` 187, `at_getb` 347, `at_span` 342, `at_parse` 3,
`at_compose` 2, `at_draw_line` 2. Against 144 ms that is roughly `at_glyph` 11%,
`at_parse`'s styled loop 12%, `at_getb` 8%, `at_span` 5% — so the remaining
wins are 5-12% each and none is a cliff.

**S25's SECOND HALF IS BUILT** (§46.4.8) and it took S25's flag after all — but
only after measuring the version WITHOUT it. The safe form, proving plainness
in `at_caret_on` with `at_slice` + the §46.4.7 scan, measured **1.02x for 65
bytes**: the proof cost nearly what the parse did. Moving the proof to
`at_scan`, which is already walking every byte to measure the wrap, is what
makes it free at the point of use — `at_parse` tests one bit instead of
scanning, and `at_caret_on` skips the parse and the slice both. Together the
mechanism is **1.22x** (140.5 → 115.6 ms), of which the flag is the last 3.7 ms.

Two things worth keeping from it. **`AT_SPECIAL` is why the two predicates
cannot drift** — the four characters are listed once, and a fifth delimiter is
one edit rather than two files that can disagree silently. And **the wrap
rewind is a performance refinement, not a correctness one**, which looks like
the opposite: `at_scan` walks past the break before rewinding, so without
banking the flag at each space a delimiter beyond the break would clear it —
calling a line styled that could have been raw. The flag can never wrongly say
*plain*, because every character of the line was scanned before the decision.

**AND THE REAL PRIZE IS NOW MEASURED AND IT IS `at_scan`.** Bracketing single
calls on the shipped build: **one `at_parse` is 5.06 ms** and **one `at_scan` is
35.5 ms — 29.8% of a 119 ms keystroke in a single call.** Nothing else is
close. Inside it, `at_respan` is the plan's own Wave 5b: it re-walks each
wrapped visual line to re-derive a span nibble both break paths already know,
which on a three-line paragraph is two extra walks of ~74 characters through
`at_getb`. `at_slice` is NOT a target — `at_copyout` already splits at the gap
and uses `rep movsb`.

**WAVE 5b IS BUILT** (SPEC.md §46.3.2) and it is the best byte-for-byte trade
of the whole series: **1.11x** on a three-visual-line paragraph and **1.13x**
on a five (115.6 → 104.6 ms and 205.2 → 181.7, Hercules) for **25 bytes**. It
scales with the number of WRAPS, because that is how often `at_respan` ran.

`at_scan` carries the span nibble in DL as it walks; at a wrap it rewound the
position and then called `at_respan`, which walked the whole visual line AGAIN
to recompute what the first walk had already had. The obstacle was only that
the walk runs past the break before rewinding — so the state is banked at every
space and rewound, which is the same answer §46.4.8 uses for its plain flag. A
space changes neither the nibble nor `[at_scskip]`, so the state after
processing it IS the state at the break; on a hard break there is no rewind at
all and DL is already the answer.

**The case it must be tested with is a delimiter in the OVERSHOOT** — the
characters walked past the wrap before the rewind. The gate was green with the
rewind deliberately removed until the document gained a line carrying one `*`
every four characters: the wrapped bold run already there has its delimiters at
the ends and never enters that region. That is the second time in this plan a
break-test passed for the wrong reason, and both times the fix was a witness
rather than a better assertion.

**AND `at_getb` IS OFF `at_scan`'S WALK** (SPEC.md §46.3.3): the segment is
loaded once for the length of the walk and the gap resolution is inlined, which
is **1.04x for 22 bytes** (104.6 → 100.2 ms on three visual lines, 181.7 →
174.2 on five). `at_slice` is deliberately untouched — `at_copyout` already
splits at the gap and uses `rep movsb`, which is what this makes the walk do by
hand.

Its witness is the HIGH run: the gap sits at the caret, so a document typed
forward reads the low run only, and breaking the gap arithmetic on purpose left
the row green until it gained an edit with text still AFTER the caret. **Third
time** a break-test in this work passed for want of a witness.

**AND THE PER-CELL CALL IS OFF A PLAIN LINE** (SPEC.md §46.4.9). §46.4.3 took
the decisions out of `at_glyph`'s body; this takes the *call* out. One
`at_glyph` is **1,034 cycles** and its eight-row emitter is **264** of them —
the other 770 are a `call`, seven push/pop pairs, a range check, a style
dispatch and a strip cursor recomputed from `at_xmap`: **74% overhead on the
one path with no decisions left to make**. On a line `at_parse` took `.rloop`
for, at scale 1, `at_vis` and `at_sty` are zero everywhere and cell *i*'s byte
column IS *i*, so `at_compose` runs a straight-line emitter and the cursor is
an `inc`. **1.22x for 106 bytes** (99.4 → 81.8 ms, three visual lines,
Hercules; 19,747 → 19,853).

Its two guards break differently and that is the whole test story. Clearing the
flag at `.styled` is worth **10,705 differing pixels** the moment it is
removed — the styled lines of the document are on the glass in every scene.
The **scale** guard is worth nothing at all until a PLAIN line is rendered at a
scale above 1, and `tests/atblit.py` did not have one: the zoom scene captured
only after the round trip back out, and capturing *while* zoomed in did not fix
it either, because the caret's line at that point is the delimiter-per-four-
characters one and `at_parse` clears the flag on it. **Fourth time in this plan
a break-test passed for want of a witness**, and this one took two attempts to
witness rather than one.

The scene now pages to the document's plain filler tail and **says so out of
`at_lattr`**, which is the reading that is not confounded: SPEC.md §46.4.8 lets
`at_caret_on` skip `at_parse` on a plain line, so `[at_pplain]` after a repaint
is whichever *styled* line was drawn last and reads 0 with a perfectly good
plain line on screen. It is a search rather than a count of PageDowns, because
the three adapters fit different numbers of rows and because the very bottom is
one line short of useful — `at_layout` emits the trailing empty logical line
through `at_emitend` without `at_mkplain`, so its attr is 0 and a view at
`at_maxtop` shows that line and nothing else. With the guard removed the scene
now reports **85 differing pixels** in one cell.

**Where the keystroke stands**: 520.7 ms at the start of this work, **81.8 ms**
now on the same scene — **6.37x**.

**4a IS REFUSED, and the measurement that refuses it found something better.**
Re-costed on iron-equivalent numbers, Hercules, text region 592 x 284:

| | measured |
|---|---|
| one `gfx_scroll` of the text band | **176.6 ms** (842,711 cycles, identical across three) |
| the full-region white `GFX_FILL` it would replace | **~115 ms** |
| one `at_draw_line`, one-character line | **6.1 ms** |
| one `at_draw_line`, full-width plain line | **18.9 ms** |
| one `at_redraw_below` over 22 such lines | **249.6 ms** |

**The scroll costs MORE than the fill it replaces** — 176.6 against 115 — so
4a's saving is not "a scroll instead of a repaint", it is `(N-K)` line draws
minus a 61 ms surcharge. That was still a good trade at the 122 ms line the
plan was written against, and it is a marginal one at 18.9: break-even at
`N-K` >= 4 full-width lines *before* §46.4.10, and >= **9.3** after it, on a
view that holds 28. An edit below the top third of the screen would pay MORE
than it does today, and a document of short lines can never reach break-even at
all (29 lines of a 28-line view). Against +230 to +300 bytes and the dy-anchor
hazard, that is not a change worth making.

**§46.4.10 IS WHAT THE RE-COSTING FOUND**, and it is the better half of 4a for
none of the risk. Both whole-view repaints opened with a white `GFX_FILL`
across the entire region and then drew an **opaque, full-width** line into
every row of it — `at_draw_line` blits `CX = [at_tw]` on the band arm and
expands to the same width on the colour one, and consecutive lines tile
exactly. So every pixel between the fill's top and the bottom of the last line
was **written twice**: PERFORMANCE.md's second rule, broken in the two places
that repaint the most. The fill moves *after* the loop and covers only what the
text did not — `at_line_y` with `BX = at_nlines` is the first row it did not
reach, and CF there means the view is full and there is nothing to erase.

**1.33x on `at_redraw_below` for 24 bytes** (249.6 → 187.4 ms over 22 lines;
259.0 → 195.4 over 23, 239.2 → 181.4 over 21). Factoring the two inline fills
into `at_filltail` pays for most of it. `NOATTAIL=1` is the A/B.

**The obvious formulation of the fill top is wrong, and the row caught it.**
`at_line_y(at_nlines)` — the row after the last line — is the bottom of the
drawn text *only while the loop ran to the end*. A loop that stops early leaves
every row between the last line it drew and that answer unerased: 130 differing
pixels on the scrolled scene and 502 on the undone one, all of them old text
nothing had painted over. `[at_tby]` carries the row instead — `[at_dly] +
[at_prh]` after each line, which is the y `at_parse` was just given and the
height it just computed — for 2 bytes of bss and one `add` a line, and it
cannot be wrong about a loop it is inside. **Fifth time in this plan** the
right answer was a witness rather than an argument, and the first time the
witness was already in place when the mistake was made.

**WAVE 6 IS REFUSED AS SPECIFIED, and answered by §46.4.11 instead.**
`at_append`'s ceiling is one line's *compose*: a full-width plain line is
18.9 ms and a one-character one 6.1, so the most it can return is ~13 ms of an
81.8 ms keystroke — 1.2x on three visual lines — and it wants a strip cache
with two stamps to do it. The measurement says the target is not the line, it
is **how many lines**: three at 18.9 is 57 of that 81.8, so the range is 70%
of a keystroke and `at_append` does not touch it.

**§46.4.11 narrows the range**, and it is a fourth attempt at something part 5
refused three times — so what makes it different is the whole argument. K1, K4
and K6 are all predicates over the **staging window**, and the answer is not
visible there: `at_relayout` converges on old == new at or before the edit, and
greedy wrap lets a line ending *before* an edit have its break decided by text
at or after it. This is a predicate over the **edit**, in the one case where
the wrap cannot reach backwards at all — an **append at the end of the logical
line**. Greedy wrap is prefix-determined, so no break above the caret's line
can move; if the append overflows the last line, a visual line appears and the
count changes, which is a different arm of `at_apply_edit`.

Three conditions: the edit is an append (a one-shot `[at_apnd]`, cleared where
all three arms of `at_apply_edit` meet *and* on the failed-`at_ins` path that
never reaches them), the character cannot style (`AT_SPECIAL`, so there is
still one predicate in the tree), and **every staged line is plain** — 46.4.8's
bit 3, which is what makes the argument airtight rather than nearly so: a plain
paragraph holds no `[`, so no link can be COMPLETED by the appended character,
and `]`, `(` and `)` are not in `AT_SPECIAL`.

**1.87x on three visual lines and 2.29x on six** (82.4 → 44.1 ms, 172.4 →
75.3, Hercules) for **110 bytes**. It does not flatten the paragraph out of the
cost — what remains grows with it too, because `at_scan` rescans all of it and
`at_splice` installs all of it — so the line draws were the larger half rather
than the whole.

**The row could not witness it as it stood, twice over.** Every paragraph in
the test document was styled or one visual line long, so the gate was never
taken: a plain paragraph long enough to wrap onto three lines had to be added.
Then removing the end-of-line test on purpose was STILL green, because the
document's only mid-paragraph edit is 20 characters back from the end — the
LAST visual line of its paragraph, where nothing is pushed past a wrap. Two
ArrowUps and four characters is what made it red, at 2,481 differing pixels.
**Sixth time in this plan**, and the second in a row, that the fix was a
witness rather than an assertion.

**AND THE PULL-DOWN BANKS ITS PIXELS** (SPEC.md §46.5.1) — which was not in
this plan at all, because when it was written a package had no way to read
pixels off the screen. `elendilon` published `gfx_save`/`gfx_restore` as
slots 0x0508/0x0510 for Word (§68.2.1), and ArtfulType's menus wanted the same
thing for the same reason: `at_mclose` erased the panel and then called
`at_draw_line` for every text line it had covered, FULL WIDTH — the panel is
~120px and a line is `[at_tw]`.

| | before | after |
|---|---|---|
| `at_mopen` | 53.7 ms | 66.0 (it banks now) |
| `at_mclose` | **104.9 ms** | **14.5 (7.2x)** |
| round trip | 158.6 | 80.5 (1.97x) |

250 bytes, and `at_menu_track` closes and reopens per title crossed, so
dragging File → Help is that saving four times over. Every refusal is the same
refusal and none of them is a new path: no claim, a straddling rect, a
zero-byte rect — `[at_suseg]` stays 0 and the close repaints, which is what it
did before.

`tests/atmenusu.py` is the gate and it asserts PIXEL EQUALITY, which is the
only assertion worth making about a save-under. What it took to be a real test
is dismissing the menu **without picking** — every item runs a command that
repaints the screen and would hide the error — and a second cycle that pokes
`[at_suseg]` = 0 mid-drop, so one run checks the write-back and the fallback
against one reference. Verified to go red: leaving the drop shadow's ROW out of
the bank is 68 differing pixels on exactly that row. (Word's own note is worth
keeping in mind here — its first break attempt did NOT go red, because
`gfx_save` rounds x to byte columns, so a column-sized error can vanish.)

**The modal alerts are the same shape and are NOT taken.** `at_mdclose` has
the identical repaint loop, and the About card is the largest thing ArtfulType
puts over its content — but at 340x236 on a colour adapter that is a ~42 KB
claim for the duration of a dialog, against ~1.5 KB for a pull-down on 1bpp.
The pull-down is also the one that happens constantly. If it is taken later,
`at_subank`/`at_surest` already take an arbitrary rect and need no change.

**What is left**: nothing in this plan. 2a, 2b, 4a and Wave 6 are settled and
refused; everything else is built.
