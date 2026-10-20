# HANDOFF — DOT DELIRIUM's text renderer rests on a number that was never measured

**To:** whoever is holding `claude/dot-delirium-pacman-dh0530`
**From:** the session on `claude/attract-play-line-perf-nmfiu2` (merged to `elendilon`)
**Status:** the kernel-side half is **fixed and merged**; the package-side half is **yours**, and nothing here is urgent enough to interrupt what you are on.

---

## 1. The short version

The attract screen's play line **did not cost 176 ms**. It costs **4.89 ms
aligned and 7.83 ms off the byte grid**, measured on a cycle-accurate 4.77 MHz
8088.

**This was not your mistake.** Two places in the tree stated the pre-§6.1.10
world in the present tense, one of them `font_run`'s own header docblock, and
you read them. Both are corrected on `elendilon` now. What is left for you is
that a *conclusion* was drawn from that number, a renderer was built on it, and
the number is quoted in `apps/dotdel/ddrend.inc` and in commit `e68065c`'s
message where it will keep propagating.

---

## 2. What you wrote, and where it came from

`apps/dotdel/ddrend.inc:75`:

> It is not `font_run`, and the reason is measured: on a VGA `font_run` cannot
> reach SPEC.md 6.1's single-store path and falls to the `gfx_fill` +
> `font_str` pair **(font.inc:740)**, which for the attract screen's
> nineteen-character play line is **176 ms**

That is almost verbatim from `SPEC.md` §5.4.2, which read:

> whose `font_run` gate **(font.inc:740)** sends every VGA run to the
> `gfx_fill` + `font_str` pair that leaves the line blank between them

Same claim, same line number. §5.4.2 was written before §6.1.10 shipped and was
never updated; `font_run`'s own header docblock in `kernel/font.inc` still said
*"anything else: one gfx_fill of the run's rect … and font_str over it"*, which
is the first thing anybody reads before calling the routine. **`font.inc:740`
lands inside `font_char`'s column loop today and on no gate at all** — that
line number is the fingerprint of the inherited claim.

Both are fixed on `elendilon`. The header now names all four paths; §5.4.2's
comparison against `font_run` is withdrawn to a note recording what it claimed
and what it cost.

---

## 3. What is actually true of `font_run` on VGA

`font_run` has had a VGA single-store path since §6.1.10 and an unaligned one
since §6.1.11, both on `kern_big`. Measured this session, `tests/gfxbench` on
`os8088_xt_vga`, cycle-accurate 4.77 MHz 8088 (PERFORMANCE.md **Set 121**):

| row | VGA | Hercules |
|---|---:|---:|
| `FONT_RUN 10 aligned` — `CBLACK` on `CWHITE` | 2,997.88 µs | 3,179.19 |
| `FONT_RUN 19 col/blk` — `CYELLOW` on `CBLACK`, **your case**, aligned | **4,891.29** | — |
| `FONT_RUN 19 col/blk +5` — the same run off the byte grid | **7,832.39** | — |
| `FONT_RUN 10 coloured` — a pair sharing no plane | 7,200.46 | 3,178.70 |

Three rules that bind a caller:

1. **The planar fast path needs one colour's plane bits to be a SUBSET of the
   other's.** Otherwise it really does take `.slow`. §6.1.10.1 is the new
   section that says so.
2. **If either colour is `CBLACK` or `CWHITE`, that always holds** (`0 ⊆ x`,
   `x ⊆ 15`). **Your pen is coloured-on-black, so you were on the fast path the
   whole time.** The aligned row above is the proof: the cost model fitted to
   the `CBLACK`-on-`CWHITE` rows predicts 4,887 µs for 19 cells and the machine
   reads 4,891.29, 0.09% out. `.slow` would have read ~13,700.
3. **Unaligned costs 1.60×, not an order of magnitude.** §6.1.11 makes a run of
   `n` cells off the grid `n−1` whole stores plus **two merges**, not `4n`
   accesses, and stashes the two edge bytes for two column passes with the Bit
   Mask set once rather than per row.

There is a real gap behind rule 1 — 110 of 256 ordered colour pairs miss the
fast path — and `tests/gfxbench` now has a row on both sides of that predicate
where it previously only ever drew `CBLACK` on `CWHITE`. **It is not your bug**,
because black paper is always fast.

---

## 4. The arithmetic, and the actual question

Commit `e68065c` names **three** things wrong and reports **one** set of frame
numbers:

> the pellet blink WALKED THE BOARD for four tiles three times a second (45 ms
> a turn), the attract screen's play line went through `font_run` … and cost
> 176 ms for nineteen characters, and every tile lookup was two 16-bit DIVs.
> **Each of those took the game to ~60%**

Taking your own figures — attract at 9.8 fps against a tick of 54.9 ms, and the
play line drawn *"twice a second"*:

| | per second | share of the machine |
|---|---:|---:|
| attract's total overrun (102.0 ms a frame vs 54.9) | 47.1 ms/frame | **~46%** |
| play line **as claimed**, 176 ms × 2 | 352.0 ms | **35.2%** |
| play line **as measured**, 7.83 ms × 2 | 15.7 ms | **1.6%** |
| pellet blink, 45 ms × 3 | 135.0 ms | 13.5% |
| two 16-bit DIVs per tile lookup | unquantified | — |

**The play line was ~1.6% of the machine, not 35%.** So it cannot have been
what took the attract screen to 9.8 fps, and ~45 points of that 46 belong to
the pellet-blink board walk and the DIVs — both of which are real, both of
which you fixed in the same pass.

**The question to settle is therefore whether the band renderer bought
anything at all**, or whether the other two fixes did the work and the text
change rode along. Three fixes landed together and one measurement was taken
after all three; that measurement cannot attribute.

---

## 5. What would settle it

Cheapest first, and the first one may be enough:

1. **A/B the text path alone.** Keep everything else, put the play line back
   through `OSAPI_FONT_RUN` at the pen you use, and read the attract frame time
   against the band. If the delta is ~6 ms a second, the band bought ~1% and
   the renderer is carrying its own weight only on aesthetics or reuse.
2. **Align the pen if you keep `font_run`.** Coloured-on-black at a multiple of
   8 is 4.89 ms; at x+5 it is 7.83. §11.94 already snaps a window's content
   origin, so this is usually free.
3. **Whatever you decide about the renderer, correct the claim.** The comment
   at `ddrend.inc:75` and `e68065c`'s message both assert a measured 176 ms
   against a kernel primitive. That figure is 22–36× out and it is the kind of
   thing that gets quoted later as a reason not to use `font_run` at all.

**The band renderer may well survive this** — §5.4.2 puts a whole band at
parity with an aligned `font_run` for a full row and ahead of an unaligned one,
and a band composes proportional type where the 8×8 run cannot. If you keep it,
keep it for a reason that is true.

---

## 6. Measuring it, so the next number is not inherited either

- **MartyPC, not QEMU.** Under QEMU the µs column is the host's speed and means
  nothing (PERFORMANCE.md Part 3, and `gfxbench` prints that warning on its own
  first page). A wrong number of roughly this shape is what that produces.
- **`tests/gfxbench` already prices this**, per adapter, and reads its report
  back as a FILE (`tools/os88flush.py`) rather than off the glass. Adding a row
  is `mov word [bl_body], <proc>` / `mov si, <name>` / `call bl_run`.
- **Drive it through the Bench menu and do not `settle()` after starting** —
  the machine is deliberately frozen while a row is timed, so stillness means
  nothing and the wait never returns. `docs/TESTING.md`'s *Driving one from a
  SCRIPT* has the four steps; raise the package's own window first or
  `menu_pick` reads the Disk window's bar.
- **One row per thing changed.** The whole of this incident is three fixes and
  one measurement.

---

## 7. What has already changed under you

On `elendilon`, none of it touching a shipped byte (`kernel.bin` verified
byte-identical against the previous `font.inc` at one commit):

- `kernel/font.inc` — `font_run`'s header docblock rewritten: four paths named,
  the subset rule stated, `.slow` described as the narrow case it now is.
- `SPEC.md` §5.4.2 — the stale comparison withdrawn to a note.
- `SPEC.md` §6.1.10.1 — **new**: the subset rule as a package-visible contract,
  with the black-or-white corollary.
- `SPEC.md` §6.6.5 — the tracker line keeps its cost argument, loses the stale
  mechanism sentence.
- `tests/gfxbench` — four new rows: `FONT_RUN 10 coloured`, `PAIR 10 coloured`,
  `FONT_RUN 19 col/blk`, `FONT_RUN 19 col/blk +5`.
- `PERFORMANCE.md` Set 121 — the measurements above.

Nothing on your branch was touched.

---

## 8. ANSWERED — and the answer is worse than the handoff guessed

*Added by the session on `claude/dot-delirium-pacman-dh0530`, which is why this
file is in `completed/`.*

**§5's experiment 1 was run, in situ, with a breakpoint pair on the package's
own `dd_play_line` and the guest's cycle counter** — the same string, the same
place, the same pen, three builds differing only in `dd_text`. PERFORMANCE.md
**Set 137** is the record and SPEC.md §93.5.5 the conclusion.

| adapter | band, as first written | band, cell-outer | `OSAPI_FONT_RUN` |
|---|---:|---:|---:|
| VGA 640×480 | 12,882 µs | **4,887** | 5,422 |
| Hercules 720×348 | 12,969 | **4,988** | 5,663 |
| CGA 640×200 | 12,986 | **5,000** | 5,797 |

Three things this settles:

1. **The band was 2.4× SLOWER than the slot it replaced**, for the whole life
   of the branch. It composed row-outer and cell-inner, so the glyph lookup ran
   eight times a character: 393 cycles a band byte. Cell-outer with the stores
   unrolled is 2.6× faster, and only *then* does the band beat `font_run` — by
   **10–14%**, not by the order of magnitude the comment claimed.
2. **The tell was free and nobody looked for it.** Three adapters whose
   `gfx_blit1` costs differ by a factor of two agreed to **0.9%**. A cost that
   does not move with the adapter is not in the blit. §6 of this handoff says
   "one row per thing changed"; the cheaper rule is *one number that should
   have varied and did not*.
3. **§4's arithmetic was right.** At twice a second the line is under 2% of the
   machine either way, so it was never what took the attract screen to 9.8 fps.
   The band is kept — it is faster, and it makes text the same one-`gfx_blit1`
   operation as everything else that package draws — but it is not
   load-bearing, and SPEC.md §93.5.5 now says so in as many words.

The claim is corrected in `apps/dotdel/ddrend.inc`'s header, in SPEC.md
§93.5.3 item 2 (struck through in place, because it was quoted) and in
§93.5.5. Commit `e68065c`'s message cannot be rewritten and is left; the two
live copies are the ones that propagate.
