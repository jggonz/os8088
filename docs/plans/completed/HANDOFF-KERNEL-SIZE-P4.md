# Kernel size pass 3: the record, and what is left for a fourth

**PASS 3 HAS LANDED.** Ten batches on
`claude/kernel-size-optimization-p3-261f31`, cut from `9427d16`, closing at
`08a8743`. §0 is the outcome; §1 says where the bytes came from; §2–§3 are
what is LEFT and what was refused with the arithmetic attached; §4–§6 are the
part worth most, which is what the pass learned about its own checks.

Its companions, and this file repeats none of them:

* **`docs/plans/completed/HANDOFF-KERNEL-SIZE.md`** — pass 1's handoff, still the authority on
  **method**.
* **`docs/plans/completed/HANDOFF-KERNEL-SIZE-P2.md`** — pass 2's record.
* **`docs/plans/completed/HANDOFF-KERNEL-SIZE-P3.md`** — the brief this pass ran from. **Its §1
  scope was WRONG and is corrected in place**; its §2 (the MartyPC/`make`/commit
  lock) and §3 (how to run the parallel soak) are unchanged and still the how-to.
* **`docs/plans/HANDOFF-SOAK-FINDINGS.md`** — open before diagnosing any test failure.
* **`docs/KERNEL-MEMORY.md`** — where the budgets stand, and the guard rules.
* **`docs/plans/LAST-DROP-BYTES.md`** — the live register of `.ovl`-eligible bodies
  and of merges that look available and are not. Its §7.2 is updated by this
  pass: the condition it names is now MET and the answer is still no.

---

## 0. THE OUTCOME

### The scope, first, because the brief had it wrong

`kernel/` holds **45 files**. Pass 1 took the twelve largest by compiled bytes,
pass 2 the next twelve plus four tail-sweep groups, and **pass 3 took the
remaining twenty-one** — `band` `blank` `bootprof` `clip` `clock` `clockw`
`clone` `cpudet` `desk` `dock` `dskwin` `events` `fprog` `loader` `mod`
`moudiag` `splash` `stkdiag` `toast` `viddet` `xmem`. Every one of them had
landed findings from a pass-2 *group* tail-sweep and none from a focused
agent's reading. Four are knob-only and compile to **0 bytes** in every shipped
kernel (`band`, `bootprof`, `moudiag`, `stkdiag`), so seventeen files got a
focused finder plus a cross-cutting sweep owning none of them.

`docs/plans/completed/HANDOFF-KERNEL-SIZE-P3.md` §1 nominated a different ten, on the evidence
that pass 2's finding ids contain no `F-wm-`, `F-files-` and so on. The
observation is true and the inference is not: **those ten are pass 1's twelve**,
and the ids are missing because pass 2 never covered them. That section now
carries the correction, the check that settles it (`git show 2f33456 --stat`)
and the two further defects in the same paragraph.

### The numbers — **never summed across currencies**

Base `9427d16`, HEAD `08a8743`, both measured by `tools/kernsize.py`:

| currency | section | base | HEAD | Δ |
|---|---|---:|---:|---:|
| **BINDING GUARD** | `.text` | 50,607 | 49,994 | **−613** |
| | `.bss` | 6,024 | 5,892 | **−132** |
| | **`.text` + `.bss`** | **56,631** | **55,886** | **−745** |
| footprint only | `.cold` | 37,214 | 36,660 | −554 |
| | `.lowbss` | 9,182 | 9,182 | **0** |
| | `.vgabuf` | 848 | 848 | 0 |
| true reduction | `.ovl` | 1,425 | 1,417 | −8 |
| | `.ovlw` | 5,234 | 5,037 | −197 |
| boot only | `.boot2` | 2,439 | 2,250 | −189 |
| module | `.modc` (`CTRL.DRV`) | | | −70 |
| | `.modl` (`CLONE.DRV`) | | | −6 |

```
kern_big  .text+.bss 55,886 of KERN_CODE_MAX 65,536 -> 9,650 left  (was 8,905)
          KERN_SIZE 109,056 of KERN_BUDGET 129,536 -> 20,480 spare (40 steps)
          accrued: image 78/512  cold 308/512  low 478/512  vgabuf 336/512

kern_small .text 40,049  bss 5,392  cold 33,986  lowbss 8,712
          .text+.bss 45,441 of KERN_CODE_MAX 65,536 -> 20,095 left
          KERN_SIZE 94,720 of KERN_BUDGET 107,520 -> 12,800 spare (25 steps)
```

**`KERN_SIZE` fell by 1,024 — two rungs, and neither was uncrossed by any single
batch.** The image rung came back at B4 and `.cold`'s at B7. The image rung
needed **311** bytes of guard and the largest single batch moved 204; `.cold`'s
needed **350** and the largest single batch moved 206. So **that is the "a byte
costs a byte" rule running in our favour for once** — three batches spent into a
rung that a fourth then cleared, and no batch could have claimed the 512 as its
own.

**`kern_small` fell 96,256 → 94,720 — three steps.** Its image rung uncrossed at
B2 (46,592 → 46,080, 22 → 23 steps of spare); the other two steps are **not
attributed batch by batch**, because B2's is the only commit in the pass that
carries a `kern_small` figure. Re-measure with `kernsize -DKERN_SMALL` rather
than apportioning them.

**The binding guard is what to quote: −745.** `KERN_CODE_MAX` is the 64KB
window that offsets are 16 bits wide for, and it **cannot be raised at all**;
`KERN_BUDGET` has forty steps. `.cold` −554 sits beside that figure and is
never added to it.

### THE `.lowbss` CROSSING IS NOT THIS PASS — say so wherever it appears

`tools/kernsize.py --bless` prints, for `kern_small`:

```
*** the low rung CROSSED: 19 -> 20 steps of 512 - 512 bytes of every machine's RAM, gone ***
```

**That crossing does not belong to pass 3, and quoting it against this pass is
wrong in both directions — it is not a cost the pass paid and it is not a rung
a future pass can win back by undoing anything here.**

* `.lowbss` measures **9,182 at `9427d16` and 9,182 at `08a8743`** on
  `kern_big`, and **8,712 at both ends** on `kern_small`. Pass 3's `.lowbss`
  delta is **exactly 0**, and the 34 bytes left in that rung are exactly as it
  found them.
* The document's blessed baseline is pass 2's close — 8,798 big, 8,328 small —
  which is **384 bytes behind in both**, from the **worker-stack-slots merge
  that landed before this pass began and did not bless**. It decomposes, to
  within one byte of alignment padding: `sch_stacks` re-cut from seven uniform
  384-byte slices into `SCH_PARTITION`'s thirteen classed ones, **2,688 → 2,816
  (+128)**; SPEC.md §9.10's `mou_pstack`, **+128**; SPEC.md §8.5's
  `sch_chstack`, **+128**; `sch_chbusy`, **+1**. `kernel/stkdiag.inc` arrived in
  the same merge and contributes **nothing** — it is `STKDIAG=1` only.
* Every `kernsize` run throughout the pass printed `lowbss +384`, **including
  the very first**, taken on the base commit before a line was edited, where
  `KERN_SIZE` was `+0` and no rung was crossed.

So the bless is **the document catching up to a merge**. It is recorded that way
in `docs/KERNEL-MEMORY.md`.

### It did not cost speed, and three rows got faster

B3's three hot `viddet` rows were timed directly on MartyPC's cycle counter —
entry to the caller's return address, same forced input on both kernels,
minimum fifteen samples a line, before/after sample sets disjoint on every one —
because each is 1–6 clocks a call against `gfxbench`'s ~0.1% resolution:

| | CGA | Hercules | VGA |
|---|---|---|---|
| `gfx_rowbase`, table hit | 136 → 127 | 132 → 130 | 132 → 128 |
| `gfx_ink` | 91 → 87 | 91 → 87 | planar: not reached |
| `gfx_rowbase_calc`, miss | | | 365 → 348 |

**One reading is unexplained and is reported rather than explained away**: CGA
`GFX_PIXEL` is **+0.19%**, and an A/A control repeats to 2 counts, so it is not
noise. Against it, the two routines that row calls are exactly −13 cycles, VGA's
`GFX_PIXEL` moved −3.9 clocks/pixel against a −4 prediction, and CGA
`FONT_CHAR` moved −1.71% — equally unattributable in the other direction. A
third kernel padded so every downstream routine keeps its old address reproduces
both readings, so it is not code motion. The residual is confined to 1bpp
framebuffer-touching rows; on Hercules a VRAM-write row moved −10.42% between
two kernels on a byte-identical bench package whose loop the kernel never
enters, which is the same artefact an order of magnitude larger. CGA's net over
71 finely-timed rows is −0.07%.

---

## 1. WHERE THE BYTES CAME FROM

Every figure is `kernsize`'s own, taken at the commit. Each batch was gated
twice — once by its implementer and once independently — with `gate.sh`'s seven
steps, of which step 7 is `make test-full`.

| commit | batch | `.text` | `.bss` | **guard** | `.cold` | other | `KERN_SIZE` |
|---|---|---:|---:|---:|---:|---|---:|
| `4f2e749` | **B0** the gates five later batches lean on, and two live-bug repairs | 0 | 0 | **0** | −13 | | 110,080 |
| `37d3a05` | **B1** the splash, and the boot canary's floor moved out of the pass's way | 0 | 0 | **0** | 0 | `.ovl` −4, `.boot2` −185 | 110,080 |
| `2b162c7` | **B2** `viddet`'s six dead display cells; `VID_CTX_W` 19 → 16 | −75 | −12 | **−87** | 0 | | 110,080 |
| `157d60c` | **B3** `viddet`'s encodings, three hot rows measured rather than argued | −87 | 0 | **−87** | 0 | | 110,080 |
| `dd01b18` | **B4** `dock`, `fprog`/`wm`, `toast`/`blank`/`driver`/`vga12` | −202 | −2 | **−204** | −17 | `.ovlw` −16 | **109,568** |
| `02858c5` | **B5** the event ring and the clipboard | −85 | −4 | **−89** | −28 | | 109,568 |
| `78710ab` | **B6** `desk.inc`, and a refusal that turned out to be a proof | −4 | −6 | **−10** | −153 | `.ovlw` −5 | 109,568 |
| `178ae84` | **B7** the three loaders, and the regression row the fence repair owed | −1 | −68 | **−69** | −206 | `.ovlw` −24 | **109,056** |
| `040b8c9` | **B8** the clock ladder and the XMS half | −102 | −29 | **−131** | −8 | `.ovlw` −126, `.modc` −67 | 109,056 |
| `08a8743` | **B9** the cross-cutting sweeps, and the `jcc` census re-taken | −57 | −11 | **−68** | −129 | `.ovlw` −26, `.ovl` −4, `.boot2` −4, `.modl` −6, `.modc` −3 | 109,056 |
| | **total** | **−613** | **−132** | **−745** | **−554** | `.ovl` −8, `.ovlw` −197, `.boot2` −189, `.modc` −70, `.modl` −6 | **−1,024** |

**The first two batches moved the binding guard by ZERO on purpose**, and what
they bought was not bytes:

* **B0** is gate work plus two live-bug repairs. `loader.inc`'s `img + bss`
  fence wrapped 16 bits and its comment was the bug stated as its own proof
  (*"img+bss <= 0x1E000: no wrap"*, where 0x1E000 is 17 bits): a crafted `.O88`
  with image = 0xF000 and bss = 0x1001 sums to 0x10001, wraps to 1, passes the
  compare, sizes a **one-kilobyte** claim, and step 6 then reads 61,440 bytes
  into it — 60,416 bytes through whatever `mem_claim_hi` placed below, which is
  a resident package's code, because it places top-down. SPEC.md §21 states that
  write bound as an invariant twice and the carry is what makes it true. The
  repair **pays for itself at −12 bytes**. Two things went with it that were not
  known when it was filed: `.big2` was **not** dead beforehand (image 0xF000
  with bss 0x0200 reaches it), and it was the one exit in `ld_load` reached by
  `ja` rather than `jc`, so it answered CF=0 with `AL = LD_EBIG` — a failure
  returned as a success, harmless only because `loader_run` reads AL and never
  the flag.
* **B1** is `.boot2` and `.ovl`, **neither of which is in either guard**. What it
  actually bought is that the rest of the pass was legal at all. The `KSIG_OFF`
  comment in the `Makefile` said in capitals that `.text` **may not fall below
  50,178** without leaving `KSIG_OFF` = 50,176 no legal value — and crossing that floor is not a loud
  failure by construction: below it the canary word lands in the `.text`-to-`.bss`
  zero padding, where every word equals its neighbour a sector away, so
  SPEC.md §18.93.1's probe **passes on the one fault it exists to catch**.
  `.text` was 50,607 and the pass takes −613. The splash set took `.boot2`
  2,439 → 2,254, which took the `SPLSTARS=1` arm from 4,193 of a 4,096-byte
  blob to 3,989, so `BOOT2_SECS_STARS` is retired with the second `OVL_AT` and
  the Makefile's second `sed`; one blob length widens the legal band and
  `KSIG_OFF` is **6,656**, floor **6,658**. `tests/unit/t_canary.py` now reads
  every `BOOT2_SECS*` equate and requires the offset to cross on all of them —
  a rule that existed only as a Makefile comment and was enforced by nothing.

**This was found by the integration seat and by nothing else.** No single-batch
review could see it: it is a property of the whole pass's `.text` delta against
a constant in the Makefile, and the remedy sat three batches too late in the
plan as written.

---

## 2. WHAT IS LEFT

### 2.1 The three owner decisions, re-priced and still open

None was taken. Each wants its own commit.

| | guard | `.cold` | `.ovlw` | `.modc` | state |
|---|---:|---:|---:|---:|---|
| **`F-clock3-01`** — `clk_ns_stamp` out of `.text` | **−65** | | +60 | +60 | **the price went UP** |
| **`F-toast3-01`** — `toast_say` `.text` → `.cold` | **−35** | +27 | | | the safest of the three |
| **`F-xmem3-01` arm B** instead of the arm A that landed | **−6** more | +4 | | | recommendation is still arm A |

**`F-clock3-01`.** The currency argument is the strongest in the pass — 65 bytes
of the binding guard for 120 bytes of two transient images, one of which is
forfeit at the first mount — and the caller analysis was verified site by site
in the adverse phase. **Its mitigation does not assemble.** The filed form is
"one `%include`d source fragment assembled twice"; both expansions define
`clk_ns_stamp:` and NASM answers `label inconsistently redefined`, **rc=1**,
proved with nasm rather than argued. It needs a `%macro` taking the entry name
as an argument, and two consequences follow:

* a plain `%include`d file that emits code needs a row in `os88ovlchk.py`'s
  `EXTRA` map, and **`EXTRA` maps a file to exactly one section**, so a fragment
  included into `.ovlw` *and* `.modc` is not representable there at all;
* with the `%macro` form **both emitted labels are invisible to ovlchk's label
  map**, so neither copy is covered by the near-call check or the return-kind
  check.

SPEC.md §37.93 refused the same trade at 24 bytes. And **the −65 is gross**:
`F-clock3-15` supersedes into it and already landed inside B8's −102, so the
**incremental** gain today is **−60**.

**`F-toast3-01`.** `toast_say` is 35 bytes with no `.text` caller and no indirect
one — no `dw toast_*` table entry, no `OSAPI_*` slot. It is the rare case where
`os88ovlchk`'s return-kind rule is **not** blind (the body keeps its own `ret`),
so any surviving `call KERNEL_SEG:toast_say` fires *"far-called, ends in a NEAR
ret"*. Name the thunk **`toastf_say`** — CLAUDE.md's label hygiene and the
tree's own `<prefix>f_<name>` convention. Price the stack: the `.cold` path
gains +2 and the `.modc` path +6, both on `STK0_TOP`. `.cold`'s rung has **204
bytes left** at HEAD, so +27 crosses nothing today. SPEC.md's toast section
needs correcting either way: it names a `cw_toast_say` that does not exist and
an `assoc.inc` caller that does not call it.

**`F-xmem3-01` arm B.** Arm A landed in B8 at −9 (`snd_release_rec` and
`xm_release_rec` are always called as a pair; the finder nearly filed it wrong,
having found a fourth caller its own header did not know about, and an adverse
re-derivation confirmed there is no fifth). Arm B is 6 more bytes of guard for
+4 `.cold`, is **worse on `kern_small`**, and rewrites `ld_unreserve`'s head,
which a loader row already rewrote.

### 2.2 The `drv_load_row` ≡ `mod_need` merge — refused with arithmetic, handed forward MEASURED

`F-xcut3-12` (~−50, HIGH) and `F-loaders3-02` (~−20) are **the same merge filed
twice**, and `F-xcut3-12`'s own arithmetic was broken in both directions — it
priced the parameterisation at 12 bytes where it is 31 and under-counted the
shared body by about the same, two errors of opposite sign that happened to land
near the right answer.

**It was BUILT and measured in the adverse phase**, with the real class block and
failure ladders so the `jcc` distances are honest. The refusal record gives
**−53 `.cold`, −4 `.bss`**; an earlier entry records the same build as
**306 → 257 = −49 `.cold`**. *Those two figures are not reconciled in the record
and whoever takes this should re-measure before quoting either.*

**Why it was refused, and it is arithmetic rather than nerve:** it subsumes
smaller rows already worth −24/−4 and collapses an −8 row, so its **increment is
~−21 `.cold`** — a currency that does not bind — for the pass's single
highest-risk change: the boot path, a `%ifdef KERN_BIG` bracket that has already
cost one silent build, and a `mov cl, 10` that assembles cleanly and is wrong.
The small rows were taken instead.

**Pass 4 starts from a better position than the one that got it refused.** The
premise the merge needed — a shared `ld_fsz` — was **directly refused by its own
author** (*"a proof of mutual exclusion I could not construct"*). That premise is
now **MOOT**: both sides carry the size in **BP** and neither `.bss` word exists
any more, so the merge's increment is smaller than the −21 it was refused at.

**Carry both hazards forward verbatim; neither finder named them.**

1. **`mov cl, 10` destroys CL before the `MEM_K_*` tag is needed**, so the
   obvious construction silently rewrites the heap owner tag **0xFF03 → 0xFF0A**
   — and **assembles cleanly**. If taken, it must be the BP form, with
   `push bx / mov bx, cx` above the `mov cl, 10`.
2. The shared-`ld_fsz` premise above. Carrying the size in BP removes it, is
   4 bytes better, and kills both `.bss` words.

**It must not be batched with peepholes.**

### 2.3 The `jcc` residual — and a finding about method the handoff did not expect

NASM expands an out-of-reach conditional to `7X 03 E9 xx xx`: 5 bytes for 2.

| | |
|---|---:|
| counted before the pass began | **184** |
| after nine batches had landed | **179** |
| after B9's conversions | **161** |

**The nine batches pulled back FIVE, where pass 2 saw 306 fall to 261 and pulled
back forty-five.** The reason is what kind of pass this was: pass 2 removed
whole bodies; pass 3 is a distributed one- and two-byte sweep across the tree,
and **a byte deleted 900 bytes away from a jump does not move it across the
±127 boundary**. The handoff's rule — count AFTER the batches, never before — is
right for the opposite reason to the one it gives. Here obeying it cost nothing.

**24 clusters wanted only a LABEL** — an unconditional `jmp` to the same target
already within short reach, so a label on it is 0 bytes and the `jcc` goes 5 → 2.
**18 were taken for −54 bytes with zero new code**: `.text` −18, `.cold` −12,
`.ovlw` −18, `.modl` −6. Six were refused, each with a reason rather than a
shrug:

* **two because the two `.out`s are different NASM locals** — a local belongs to
  the last non-local label, and the census matches target *text*. **nasm said
  so**, by name, twice.
* **three because the jump and its target sit in different `%ifdef` arms** —
  which would have failed only in `kern_small`, and nothing but `make test-full`
  builds that.
* **one on the hot rule.** `gfx_line`'s Bresenham `jge .pixel` has both arms as
  the loop's normal continuation, so the branch is ~50/50; re-aiming trades 12
  clocks on the taken half for 12 on the other.

The arithmetic behind the hot rule, applied and not waved: a 5-byte expansion is
`j<inverse> +3` + `jmp near`, so **taken = 19 clocks, not-taken = 16**; after
re-aiming it is a 2-byte `jcc` to a trampoline, so **taken = 31, not-taken = 4**.
Where the taken arm is the rare one it is a win twice over, which is why four
sites in hot files were taken.

**What is left, costed.** 161 expansions is a **483-byte theoretical ceiling**,
and the honest number is much lower:

* **75 bytes** sit in 15 clusters one NEW trampoline could cover (`3n − 3`
  bytes each, so worth nothing at n = 1).
* **The two biggest clusters are not reachable at all**: `vga12.inc`'s 15 jumps
  to `.no` span 0x2257–0x2BED (2,454 bytes) and `sched.inc`'s five to
  `sch_resume` span 0x4B06–0x4CD5. No single trampoline covers either, and one
  per group collapses the arithmetic.
* The two largest *reachable* ones were left on **placement, not risk**:
  `gfx_blit1_x`'s five-deep argument refusal ladder (12 bytes) and the band
  composer's `.none` (9), both `.cold` in `vga12.inc`. Both ladders fall straight
  through into the routine's body, so a trampoline cannot go after the last
  `jcc` — it has to go below the first unconditional transfer, and in
  `gfx_blit1_x` that is a long way down. A restructuring job with a 12-byte
  prize wants its own sitting.
* **87 of the 161 are in hot files** (`vga12` 54, `mouse` 18, `font` 9,
  `sched` 5), and CLAUDE.md's rule treats `gfx_*`, `mouse.inc` and `sched.inc`
  as externally hot by default.

**So the honest ceiling is nearer 220 bytes than 483.** A pass 4 that quotes 483
is quoting a ceiling whose two largest clusters are unreachable at any price,
whose next two are refused on placement, and 87 of whose 161 sites fall under
the hot rule.

**Re-derive the attribution before using any per-file figure.** The census tool
was wrong once in exactly the dangerous way: its include-stack tracker treated
any line without a leading line number as depth 0, so a blank line inside an
include truncated the stack and **every jump in `vga12.inc` was filed under
`kernel.asm`** — while the total stayed right throughout. B9's version
re-derives each expansion's (file, source line) from the listing, then **opens
that file at that line and asserts it is a conditional jump**: 179 of 179, 0
mismatches.

### 2.4 The largest remaining machine-detectable duplication is NOT in this pass's files

A corrected duplicate-run scan over the assembled `.text` (50,607) and `.cold`
(37,214) at a 24-byte minimum finds **~1,142 redundant bytes tree-wide**, and
almost none of it is in the twenty-one:

* **`gfx_line_fast`'s four octant bodies share a 24-byte run four ways** (72),
  plus three more 29–34 byte pairs among them — `vga12.inc`, pass 1's file, and
  the Bresenham inner loop, so hot.
* **`font_run_x`'s three tails share 27 bytes three ways** (54) — `font.inc`,
  pass 2's file, and `font_char`'s cell loop is the hottest thing in the kernel.
* `api_copyname.done` / `wm_obscured.gone` / `wm_damage.whole` share 27 three
  ways; `inst_ico_bounce` / `inst_ico_disk` share 31.
* The 26B×4 and 44B×2 rows resolving to `dsk_vtab` are **DATA, not code** — the
  scanner cannot tell, which is why every row needs reading before it is a
  finding.

**What that means for the record**: there is no missed motherlode inside the
twenty-one, so the per-file agents' line-by-line yield **is** the yield. What it
says for a fourth pass is that the remaining duplication sits in two hot files
that have both already been swept — which is a speed question as much as a size
one, and belongs with `docs/plans/completed/GFX-REWORK-PLAN.md` rather than with a peephole
sweep.

---

## 3. THE REFUSALS THAT ARE PROOFS

### 3.1 `.lowbss` — the pass's biggest single "no", and it is now proved rather than assumed

`.lowbss` moved by **zero** and that is a result, not an omission. Three claims
were examined and all three are floors.

**`dskwin.inc`'s 3,328** — `disk_dir` 768 + `disk_icons` 2,048 + `dsk_secbuf`
512, the first bytes of the rung (SPEC.md §2.1.2). Every term is pinned by
something outside the file:

* **`DSK_ICO_SIZE` = 64 is the icon format** — 16 mask + 16 data words, the same
  64 a `.o88` header carries and an `ASSOC.DAT` row hands over.
* **`dsk_secbuf` = 512 is a sector**, set into BX at 19 sites across five files.
* **`DSK_NENT` = 32 IS ALREADY ITS FLOOR.** `files.inc:626` asserts that
  `nmax × DSK_DE_STRIDE` is a multiple of 256 because `FS_IOFH` holds an icon
  base in ONE byte. With `DSK_DE_STRIDE` = 24, `gcd(24,256) = 8` reduces that to
  `n ≡ 0 (mod 32)`, so **the next legal value below 32 is zero**. Going lower
  means moving `FS_IOFH` to a word first (+4 bytes in the *binding* segment) and
  then arguing for a listing cap below 32, which is user-visible — a folder with
  more entries than the cap silently loses the rest.

Sizing the window at mount time frees nothing: `disk_dir` is at a fixed offset
in `LOW_SEG`, which every rung above it is derived from at assembly time. Making
it a heap claim needs **two directory passes** (a 1.44MB root is 14 sectors, and
PERFORMANCE.md prices an `int 13h` at ~400 ms, on the boot mount before the
desktop). It is also **accounting fraud rather than a saving**: the first mount
is at boot, so the claim is taken on every machine and never released, and the
RAM leaves `KERN_SIZE` only to reappear in the heap — which is what
`tests/kernresident.py` exists for.

**And the `.ovlw` guard beside `SKB_DSK` in `kernel.asm` is what settles the
envelope — with one correction the pass's own `.ovlw` −197 has since forced.**
The guard is
`((OVLW_SIZE + 511) / 512) * 512 > FAT_PARA * 16 + DSK_WIN_BYTES`, and the
region is 4,608 + 3,328 = **7,936**. Measured at the pass's **base**, `.ovlw`
was 5,234 → rounds to **5,632**, against a post-removal region of 5,120 — so it
**fires**. At **HEAD** `.ovlw` is 5,037 → rounds to **5,120**, so the same
removal lands **exactly on** the guard and does **not** fire, at **zero margin**
and with `.ovlw` free to grow again. **Do not read that as the removal having
become available**; read it as the real shape of the thing:

> **Every byte taken out of the mount window is a byte of `.ovlw` headroom
> spent, one for one.** They are the same bytes seen from either end, and a
> pass that shrinks `.ovlw` does not create room in `.lowbss` — it moves which
> of the two runs out first.

**`viddet.inc`'s 696-byte `vid_rowtab`** — narrower entries are impossible (max
entry 32,316 is 15 bits and even, so `entry/2` is still 14); **fewer** entries is
PERFORMANCE.md Set 106's measured decision, built and reverted at exactly the
predicted +512; and overlaying is impossible because **`gfx_rowbase` reads it on
the fixed cost of every drawing call, for ever**.

**`events.inc`'s 128-byte `evq_buf`** — halving `EVQ_CAP` is refused on the
record; `EVQ_RECSIZE` 8 → 7 breaks the power-of-two mask at three sites;
absolute pre-biased ring pointers (−12 `.text`) need 256-alignment and cost 42
pad bytes of the rung, **net +30**; and deriving `evq_count` from head and tail
drops usable capacity 16 → 15, which is a behaviour change.

Nothing in `.lowbss` is modal against `disk_icons` either — `vid_rowtab` and
`font_glyphs` are live from init to power-off, and the task stacks are not
negotiable — so there is no overlay to take.

### 3.2 Two rows the adverse phase killed, and one refusal that became a proof

**`F-xcut3-04`, the `gfx_lock_mine` helper. REFUSED independently by two
reviewers.** `wm.inc` and `fprog.inc` really do contain seven byte-identical
instructions, **but the two jump targets ARE the semantics** and they partition
three lock states differently:

| state | `wm_lk` | `fpg_arm` |
|---|---|---|
| FREE | take it | proceed |
| MINE | do **not** take | proceed |
| SOMEONE ELSE'S | take it (`gfx_lock` waits) | refuse |

`fpg_arm` asks `{free,mine}` vs `{other}` and its half of the conversion is
right. `wm_lk` asks `{free,other}` vs `{mine}`, which **no single flag out of
that helper can express**. What would have shipped: with the lock already held
by this task — the common case — it returns AL = 1 **and calls `gfx_lock`
recursively**, on a routine whose own banner says it is not re-entrant; and with
the lock held by another task it answers "it was ours", so `wm_show_b` runs its
`gfx_save` with **no lock at all**, which is `docs/FIELD-NOTES.md` 34.1's defect
reintroduced. The correct three-state helper was then priced at **−2** and
refused too: two bytes for a failure mode that is a hung UI task, invisible to
every gate, in the exact place the field bit once. **Two dead push/pop pairs
were taken instead, for −4.**

**`F-xcut3-11`, `desk_draw_zone`'s centring with `shr`. REFUSED**, and the
refusal is the more instructive half. `DESK_ZW` is 32 and `font_width` returns
`8 × length`, so `32 − w` is **negative for any label of five glyphs or more**
and `shr` turns `zx−12` into `zx+32756`. `font_run` is opaque and **the caption
pen is not clamped — only the white rect is** — so the result is a white smear
outside the damage rect that nothing repaints. Its stated invariant was the
wrong one: **parity is not what separates the two forms, SIGN is**, and two of
the five precedents it cited use `sar` under the comment *"SIGNED: a title wider
than its window"*.

**And five documents got the same fact wrong on the same search.** The file's
own comment, the finder, the adverse reviewer, the ledger and the final plan all
said no volume label is long enough to reach the negative case. **Everybody
checked `kernel/`; nobody checked `drivers/`** — `drivers/ramdisk/ramdisk.asm`
supplies a label of its own through `OSAPI_VOL_ADD` and nothing in the kernel
bounds its length. The verdict improved anyway, from a hope into a proof:
`font_width` returns 8 × length, so `w` is **always even** and `sar` is
bit-identical to the shipped arithmetic for all 45 inputs, while `shr` diverges
in 20 of them, first at five glyphs, pen 272 → 33040.

---

## 4. WHAT NO GATE CAN SEE — measured, not asserted

`tools/stkbalance.py` is the gate most of this pass's confirmations named. **Its
lexer matches the MNEMONIC, not the operand** (`^(push|pusha|pushf)\b`), and its
walker propagates one integer. So it catches exactly three things:

* a push deleted without its pop;
* a label reached at two different depths;
* a tail `jmp` leaving depth behind.

**It cannot see** `push ax … pop bx`; a bank deleted while the register is still
live (both halves go, so the depth stays right); any clobber-set change; any
flag change; any `mov Sreg` / DS / ES change; or a `cbw` widening the wrong byte.

**Of the 38 findings in that whole risk class, exactly ONE is genuinely
gate-covered** — `F-xmem3-02`, where a mismatched arrival depth at
`xm_free.stamp` turns `stkbalance` red, demonstrated. The liveness half of every
other confirmation is **review-only**.

That is not a reason to refuse them. **It is the reason the adverse phase is not
optional**, and it is why this pass ran adverse review grouped by shared RISK
rather than by file, and then an integration review on top. Between them they
refused two rows that would have shipped a defect (§3.2), re-priced rows in both
directions (three worth more than filed, two censuses smaller), produced **six
new findings** worth −12 guard / −16 `.cold` — of which **12 `.bss` bytes are
binding-guard currency nobody had found** — and caught the build break in §1
that no single-batch review could see.

Three more gates were audited and their blind spots measured rather than
assumed:

* **`os88ovlchk.py`'s return-kind rule was blind to 135 of 140 ladder
  routines** before B0 — `RETI` only matched a `ret`-family mnemonic inside a
  routine's own extent, and a routine ending `jmp kretfc_dx` matched nothing, so
  neither arm fired. Proved on a shadow tree carrying exactly the mistake a
  later batch could make: **rc=0, all fourteen checks green**, including *"every
  return kind matches how the routine is called"*. Six lines fixed it; coverage
  is 140/140 and the clean tree's output is byte-identical, so no false
  positives. **B6 and B8 both named that rule as their gate.**
* **`t_asmrules`' `crossed_pops` and `stkbalance` are complementary and neither
  alone covers a ladder edit.** Three mutations, measured: an orphan rung →
  `stkbalance` red, `crossed_pops` **silent**; labels swapped → `crossed_pops`
  **0 findings** (its docstring claims 2), `stkbalance` red; operands swapped →
  `crossed_pops` 25, `stkbalance` **silent**. B6 mis-placed the rung label on
  purpose in **both** directions and `crossed_pops` answered *"36 checks
  passed"* both times.
* **`dsegaudit.py --word` is blind to every claim segment but one.** It matches
  only `mov es,[w]`; every claim except one is loaded through a register, and it
  *clears* the tracked hold at that instruction. Asked about `dsk_fatseg` it
  answers *"nothing loads it"* on a tree with four such loads — **and words the
  silence as an answer.**

`t_mirror` was widened in the opposite direction: **123 → 289 names, 59 files,
305 checks, zero divergences**, which closes the whole `W_*` window record, the
whole `SSI_*` snapshot, the entire fsx ABI, every `FERR_*` and `CLIP_MAXKB` —
none of which anything in the tree compared before. The one thing blocking it
was `MENU_MAXCH`, derived in the kernel and a literal in the SDK; giving the SDK
`MENU_MAXW` and the kernel's own derivation reconciles it, and putting the
literal back turns the gate red, so the reconciliation is load-bearing rather
than decoration.

---

## 5. WHAT COULD NOT BE TESTED HERE — the whole list, and why

Every batch passed `gate.sh`'s seven steps twice, of which step 7 is
`make test-full` (the knob kernels, `kern_small`, and a boot on both 1bpp
adapters). Targeted soak rows were run per batch as well, and they are named in
each commit.

**THE FULL CLOSING SOAK HAS SINCE BEEN RUN — §9 is its result.** This section
was written when it had not been, and three of its rows have been overtaken by
it; each says so in place. What follows is still the honest list of what this
container could not answer, with the ones it can now answer marked.

**Do not read coverage into this list that is not there.**

| row / claim | why it could not run here |
|---|---|
| `dskwstage` | its default machine's ROM set was **absent from this container** (`Error loading ROM set`, before a guest instruction). Pointed at a machine that IS available it **hangs — and hangs identically on the stashed baseline, same command, same machine**. Out of reach in both directions; `docs/plans/HANDOFF-SOAK-FINDINGS.md`'s "FAIL where it means SKIP" class. **It is the row that covers `dskw_wdata.stg`, one of the sites a cross-cutting finding converted** — re-run it if you have the 5150 CGA ROM set. **ANSWERED — the ROM arrived and the answer is that the hang is NOT this pass's.** Four runs: `dskw_write_x never returned after 180s` at `8626120` on the real IBM 5150 27OCT82 ROM (in its soak chunk, and again on its own), at `8626120` on GLaBIOS, and at **`f8af49e` — elendilon before the pass merged** — on the IBM ROM. So the ROM is not the variable and neither is the pass. The 180 is a HOST wall-clock bound in `m.wait_stop`, which made load the first suspect and it is ruled out: the second failure had two other emulators up, not four. The hang itself is still open and still covers `dskw_wdata.stg`; what is closed is the question this row was parked on |
| the AT clock rung (rung 1, MC146818) | **QEMU only** — a 5150 has no RTC and MartyPC models no XT clock card, so `[clk_tier]` is 0 on both. This container had no `qemu-system-i386`. **IT HAS ONE NOW** (8.2.2, installed for the closing soak), so this rung and rung 2's refusal arm are REACHABLE here — but **still unrun, and the reason is a gap in the SUITE rather than in this container**: no registered row covers rung 1. `dtwrite` is the row that writes the clock and it declares `marty`, where `[clk_tier]` is 0, so it passes *without exercising a chip*. A row that means rung 1 needs QEMU in its `needs` and a reboot in its body (CLAUDE.md's recipe: set the clock, close the panel, `system_reset`, read the bar). Worth writing before the next pass leans on it |
| `xmcheck` / `tests/xmtest` | the one gate in the tree *verified* to fail when the XMS release calls go missing, and a RUNTIME check rather than a grep. Needs QEMU **and** a machine with memory above 1MB; MartyPC is an 8088 and `xm_sniff` returns at its first compare. **OVERTAKEN — it RAN and PASSED, 41.9 s** (§9). QEMU 8.2.2 was installed for the closing soak, so this row is no longer out of reach here and the XMS teardown is checked rather than argued. `heapmap` and `msegxms` came with it |
| clock rung 2 (MM58167) | the **refusal arm only** is reachable, via `make test RTC=ns` → `tier 0, stop 01, reg 00 = FF`, which proves the code assembles and refuses and nothing more. Not run — no QEMU |
| clock rung 3 (RP5C01) | **nothing anywhere.** No emulator in this tree has one. Field-only |
| `F-cpudet3-01` | **nothing.** A `word [ds:bp+0]` "fix" assembles, passes every gate, and reports an 8087 that exists as absent. The comment at `mov bp, sp` is the whole guard |
| B1's `xm_boot_x` table staging | no test in this container exercises the path. Verified by assembly, listing and segment reasoning — **said plainly rather than left implied**. The filed form used `push cs / pop es`, which would have copied the XMS service table **into the blob** on any machine with `XMS.DRV`, because the body is `.ovl` and far-called so CS is the blob's segment |
| `dispcheck` | fails **identically on both trees at the same line** — pre-existing, `docs/plans/HANDOFF-SOAK-FINDINGS.md`. Up to that point the two kernels produce byte-identical framebuffers on **both** cards of the extended desktop. **OVERTAKEN, and it was THIS PASS'S after all**: the closing soak found a *second*, unrelated defect in the row — it indexed word 11 of a run this pass shortened to 16 words — and fixing that (`662b429`, `docs/plans/HANDOFF-SOAK-FINDINGS.md` E4) makes it **pass**. The pre-existing timeout above and the stale index are two different things in one file; do not read this row's history as one story |

**What WAS run on a machine**, and is worth knowing exists:

* `tests/pkgfence.py` — the regression row B0's fence repair owed, with two new
  fixtures and **two positive controls**. `BSSWRAP` (0xF000 + 0xF000) and
  `BSSWORST` (0xF000 + 0x1001) both answer `LD_EBIG`; deleting the `jc .toobig`
  makes the row FAIL, and the measured failure is a clean `LD_OK` rather than
  the runaway the docstring predicted — so **the docstring was corrected to what
  actually happens**.
* B1: SHA-256 over the whole framebuffer at three points across the load, per
  adapter, before and after — CGA 640×200, Hercules 720×348 and VGA mode 12h all
  match exactly.
* B6: a whole-screen A/B on both 1bpp adapters, **0 differing pixels** of
  128,000 (CGA) and 250,560 (Hercules), **with per-zone lit-pixel counts
  alongside** — so the comparison is two identical desktops rather than two
  blank screens.
* B3: `dispmode`, `dispdepth`, `dispcp`, `dispseam`, `disptext` all pass.
* B8: `dtfield` and `dtwrite`, the only two rows that drive the Date/Time
  editor. B9: `fcpcopy` and `fmcommit`.

---

## 6. THE METHOD LESSONS — the pass's most reusable output

### 6.1 Six times a check answered confidently for the WRONG REASON

Pass 2 recorded fourteen of these and called it *the* lesson. It recurred, and
**three of the six were in this pass's own instruments** — which is where pass 2
found several of its fourteen too.

| the check | what it said | what was wrong |
|---|---|---|
| `gate.sh` step 4 | **65 unbalanced paths on an untouched tree**, with `make`, the fast tier, ovlchk, checkdocs and a real boot all green beside it | the file list omitted `kernel.asm`, **where the shared epilogue ladder is defined**, so all 141 converted routines read as unresolved tail jumps. With it: rc=0, 0 unbalanced, 3,979 entries walked |
| the duplicate-run scanner | *"0 redundant bytes in `.text`+`.cold`"* | it misparsed nasm's section-summary header and printed `regions: []` — **an empty answer indistinguishable from a clean result**. Corrected, the real figure is ~1,142 (§2.4) |
| the `jcc` census tool | a plausible per-file table, and **the right total throughout** | its include-stack tracker truncated on any line without a leading line number, so **every jump in `vga12.inc` was filed under `kernel.asm`**. The total looking right is what made it dangerous |
| `os88ovlchk`'s return-kind rule | *"every return kind matches how the routine is called"*, rc=0 | blind to **135 of 140** ladder routines (§4) |
| `t_asmrules`' `crossed_pops` | *"36 checks passed"*, on a rung label mis-placed in **both** directions | it finds **0** on that mutation where its own docstring claims 2 (§4) |
| `kernel.asm`'s `%if SKB_DSK != DSK_WIN_BYTES` | green, always | `SKB_DSK` is `equ DSK_WIN_BYTES` 266 lines above, so the condition was **X != X and no edit could make it true**. The drift it was written to catch moved both sides together and went straight through |

**The defence that worked, every time, was to break the thing on purpose and
confirm the gate noticed.** Do it for every gate you write, and write down that
you did. One diagnosis hit the trap *inside* the check that was checking the
check: `python3 tools/stkbalance.py … | tail -4; echo rc=$?` printed **rc=0**,
because `$?` after a pipeline is `tail`'s. There is no pipe anywhere in
`gate.sh` for that reason.

### 6.2 A positive control can itself be wrong, and this one would have passed

The control specified for `SKB_DSK`'s replacement guard in `kernel.asm` was *"set
`DSK_NENT` to 16 and confirm the guard fires"*. **It cannot work**: `DSK_NENT` is
on **both** sides of the new comparison, so both move together and the guard is
correctly silent. Worse, `DSK_NENT = 16` makes nasm exit 1 for an **unrelated**
reason — `16 × 24 = 384` is not a multiple of 256, so `files.inc:626`'s own
`%error` fires — and *"rc=1, therefore the guard fired"* would have been a
**false pass**. The four edits actually used are a fourth buffer, a `dsk_secbuf`
resize, a buffer moved out and inserted padding; the two the guard still misses
(a same-size substitution, and a change to the three constants) are written at
the site rather than assumed away. **The `512` must stay a literal** — spelling
it as a symbol that also sizes `dsk_secbuf` brings the tautology straight back.

### 6.3 The knob-block trap — four occurrences, and now a method

**Excluding knob-only FILES by name is not enough. A block-level `%ifdef` inside
a shipped file compiles to nothing just as surely.** It went wrong four times in
this pass: **three separate censuses counted such blocks as resident**, and the
last batch found a fourth set of sites nobody had looked for.

* The first three were `wm.inc`'s `%ifdef BANDCOMP` — 0 bytes in every shipped kernel,
  because `BANDCOMP` is defined only under `BAND` + `KERN_BIG` and `BAND` is
  opt-in. **The root cause was documentation**: CLAUDE.md carried contradictory
  `BAND=1` and `NOBAND=1` rows and the `NOBAND` row was stale. It is fixed in
  B0, and *fixing the documentation was part of fixing the error.*
* B9 found **five more sites in three new shapes**, and this is the part to
  carry: `wm.inc` has **eight** such sites, not the four an adverse reviewer
  found; `files.inc` has two in the **`%else` arm of `%ifdef KERN_BIG`**, so a
  census that tracks `%ifdef` and ignores `%else` reports the context
  **INVERTED**; `kernel.asm` has two under `%ifdef BOOT_MARK`; and `splash.inc`
  has one under `%ifdef SPLSTARS`. The last two were 2 of a finding's "nine
  verified" sites.

**The reliable test is the LISTING, not source analysis.** On the same
population, a source census got **2 of 42 wrong**; the listing check got **42 of
42** and named them.

### 6.4 Two `.bss` hazards no name-grep can see, both found the hard way

1. **A neighbour's WIDER STORE covering a byte pad.** `diskw.inc`'s `dskw_pad2`
   had exactly one mention in the whole repository — its own declaration — so by
   every grep it was dead. It was not: one of six stores to an adjacent `resb 1`
   flag was a **`mov word`**, and the second byte it wrote was the pad. Deleting
   the pad would have put a zero into `dskw_seg`'s low byte in the middle of the
   write path. Narrowing that one store to the field it means makes the pad
   genuinely free, for −1 `.cold` / −1 `.bss`. **Every `.bss` deletion in this
   pass then had to pass a test no name-grep can perform: read the declarations
   either side and every store to them, checking the WIDTH of each store against
   the width of the field.** Two other one-mention symbols turned out to be live
   table rows the same way.
2. **A HOST READER's hard-coded displacement over one.** `tests/dtfield.py` did
   **not** read the clock field by symbol, as both a finder and an adverse
   reviewer stated it did: it read `[clk_sec]` and took `year = b[8]`. Deleting
   `clk_pad` moves the year to offset 7, so **the test would have read a
   different byte while still printing a plausible date.** The first hazard is a
   neighbour's *store*; this is a reader's *offset*. A name grep sees neither.
   Both edits landed in the same batch as the deletion, and the test now carries
   a docstring saying what the offset mirrors and what a wrong one looks like —
   which is the cheap half of the fix and the half that survives the next pass.

### 6.5 A search route nobody had listed

**SPEC.md §57.4's `VD` debug block publishes POINTERS to kernel data**, so a
host tool can reach a field without ever naming it — and a name grep therefore
cannot prove a `.bss` cell is unread. The way to close it is to **read the
consumer**: `tests/dispmode.py`'s `VD` class takes 1/1/8/1/4/4 bytes from those
pointers, which is checkable, and it also asserts the kernel's published
`VID_CTX_SZ` against `os88geom`'s — which would have caught a forgotten mirror
edit on its own.

### 6.6 Two container restarts, and what each cost

* **Restart 1** killed the dock/fprog/toast agent. Its **edits survived on disk
  and its report did not.** Recovery was: verify the mandated corrections in the
  diff at source, run the full gate, and re-derive the per-unit attribution by
  A/B — revert one unit, re-run `kernsize`. The batch is committed as **gated
  and attributed but not self-reported**, which is a weaker claim than the
  batches around it and is said so in its commit message. **The cost was a whole
  batch's reasoning.**
* **Restart 2** killed the clip/events + desk agent mid-second-unit. By then the
  brief **required an incremental report and a per-unit patch**, so unit 1's
  reasoning, staged measurements and saved patch all survived; it was re-applied
  from its own patch and gated alone. The desk half was half-applied and
  `stkbalance` caught it immediately with `fdlg_onclick_x: ret at depth -1` —
  exactly what an incomplete epilogue-ladder conversion looks like — and was
  **reverted, not committed**. **The cost was the unfinished unit and nothing
  else.**

**Recommend the second shape as standing practice: an implementation agent
writes its report as it goes and saves a per-unit patch.**

### 6.7 Two smaller ones worth keeping

* **A simulation checks the function, not the interleaving.** A 22,704,447-case
  simulation proved a dock damage-span identity and could not express a
  `dock_force` arriving *mid-paint*, where the proposal's reset would have wiped
  it — a lost repaint. The corrected form is an `xchg` at the top and is **three
  bytes shorter than the proposal**.
* **A total that is right for two compensating reasons is not a measurement.**
  B7 found the plan's per-file `.cold` split did not sum to its own rows —
  `loader.inc` booked −102 against −90 and `driver.inc` −44 against a measured
  −52 — which cancelled to the right batch total.

---

## 7. THE APPARATUS — what to reuse rather than rebuild

Everything pass 2 left (`docs/plans/completed/HANDOFF-KERNEL-SIZE-P2.md`), plus:

| | |
|---|---|
| the batch gate's seven steps | `make` → fast tier → `os88ovlchk` → `stkbalance` (over `kernel/*.inc` **and `kernel.asm`**) → `checkdocs` → `kernsize` → **`make test-full`**. Step 7 was `bootsmoke` alone until integration review pointed out that **no batch would then assemble `kern_small`** — the configuration this project has *discovered* broken three times rather than had reported. Every step's exit status is tested directly and there is no pipe anywhere in it, deliberately. **The script itself was session-local and is not in the tree**; those seven steps are the whole of it and are worth re-creating rather than remembering |
| `os88ovlchk`'s ladder-aware `rets` scan | six lines, coverage 5/140 → 140/140 (§4) |
| `t_mirror`, widened by a glob | 123 → 289 names (§4) |
| `tests/unit/t_mlen.py` | twenty host-side checks of the month-length mask, **zero kernel bytes**. It exists because `tests/dtfield.py` row 3 covers **February only** — the one branch the rewrite does not change — so a flipped mask bit stays green in both arms |
| `tests/pkgfence.py` | §5 |
| `tests/unit/t_canary.py`, multi-blob-length | §1 |
| the listing-based knob-block check | §6.3 |
| the neighbour-store width check | §6.4 |

**Four knobs are in nothing's build matrix** — `NOMOUPRIV`, `NOCHAINPRIV`,
`NOUIBLOCK` and `STKDIAG`. CLAUDE.md says the knob kernels exist partly so those
arms keep assembling, and `make test-full`'s matrix is supposed to be the only
thing that builds them. **It does not build these four.** B5 built all four by
hand because its change lands inside `mou_isr`, which `NOMOUPRIV` re-routes.
Widening the roster is cheap and is not done.

---

## 8. THE RULES THAT BIT HARDEST, restated

`SS ≠ DS`, so `[bp+disp]` addresses SS. **Sections are different segments** —
`.text` is `KERNEL_SEG`, `.cold` is `COLD_SEG`, `.ovl`/`.ovlw` is the boot
overlay released at `spl_finish` and **runs at `FAT_SEG`**, `.lowbss` is
`LOW_SEG` reached through SS. Two findings in B8 would have read an `.ovlw`
table **through DS** and fed kernel image bytes to the CMOS index port as
register numbers; written CS-relative instead, one got cheaper and one dearer.
A near call across a section boundary assembles cleanly and runs wrong, and
"merge these two identical routines" is only free when both ends are in the same
section.

**A NASM local belongs to the last non-local label**, so moving code re-parents
every local inside it — and two `.out`s in two routines are two different
symbols, which is what refused two `jcc` conversions (§2.3).

**`.bss` arrives ZEROED and cannot hold a non-zero resting value.** It is
`nobits`, so `-f bin` emits nothing *for the section* and the image's own
inter-section padding lands on it — measured on the pass's base tree at **0
non-zero bytes across the 6,225-byte inter-section gap**, and gated by
`tests/unit/t_bsssentinel.py`. The rule is
two-sided: **a non-zero sentinel there is a bug**, and **an init loop that only
writes zeros into `.bss` is dead code** — which is a finding class, not a risk to
argue. `evq_init` went that way in B5. The wrong half of the claim — *"`-f bin`
zeroes nothing"* — is repeated at many sites in the tree and is the version that
makes a reader seed a sentinel into `.bss`.

**A rung is not a design input.** Quote `kernsize`'s SUM and its ACCRUED line,
never its step count. The amortised price of a byte is a byte.

**Treat `gfx_*`, `mouse.inc` and `sched.inc` as externally hot**, and cost a
branch change in clocks on both arms before taking it (§2.3).

**`.ovl` has no partial credit**, and `tests/ovlrefs.txt` + `tools/os88ovlchk.py`
are what enforce it — with the caveat that until B0 the return-kind half of that
gate could see 5 routines of 140.

---

## 9. THE CLOSING SOAK — RUN, and what it found

§5 was written when it had not been run. It has now been run in full, in this
container, against the merged tree.

| | |
|---|---|
| soak rows attempted | **200** — the whole soak tier as the suite stood |
| ok | **196** |
| FAIL | **4** |
| SKIP | **0** |
| not run | **0** |
| **regressions in kernel behaviour** | **0** |

**Zero skips is the part worth stating.** Every previous run of this suite in
this container skipped between fifteen and eighteen rows for a missing
capability, and a skip is not a pass — it is the box declining to answer. What
removed them was installing `qemu-system-x86` (8.2.2) and the C toolchain
(`tools/setup-cc.sh`, SmallerC), and building the four on-demand disks the
suite never builds for itself: `make wiredisk weavedisk loomdisk c64disk`. Two
rows would otherwise have skipped **silently inside a rate lane** — `wirefps`
and `uilat` declare a `wiredisk` capability, and `all` deliberately does not
build it.

`make test-full` also reached **51 passed, 0 failed, 0 skipped** for the first
time, because `ctoolchain`, `weavesmoke` and `ps2mouse` could finally run.

### 9.1 The four failures, each classified against evidence

Full write-ups are `docs/plans/HANDOFF-SOAK-FINDINGS.md` E1–E5. In one line each:

| row | disposition |
|---|---|
| `dispsize` | **intermittent everywhere** — 10 fail / 5 pass over 15 runs, and it fails at `f8af49e`, which predates the pass merge (E1) |
| `dskwstage` | hangs on the IBM ROM, on GLaBIOS, **and on the pre-pass tree** — closes §5's own parked question (E2) |
| `weavegame` | **identical at `f8af49e` and HEAD**; its first run ever in this container, and it carries an `ovf = 15` input overrun (E5) |
| `weavepack` | 18 of 23 checks, **identical on a loaded box and an idle one** (E6) |

**The two Weave rows are the same story twice**: neither had ever produced a
verdict here, because there was no C toolchain, and `docs/plans/HANDOFF-SOAK-FINDINGS.md`
B4/D2 record what was seen instead. Installing one did not break them — it
made them legible.

### 9.2 One mistake, and it is the reusable output

E1's bisect was **published-adjacent and void**, and the three errors that
stacked to produce it are written out at E1 because each is cheap to repeat:
the protocol at the top of that file was skipped (re-run alone, then the base,
and bisect *only* if those disagree); a row with six independent legs had its
exit code read as one verdict, hiding a passing leg C behind a failing leg E
for four runs; and the commits being bisected **did not share a base** —
three of the branches in flight fork from `f8af49e`, not from `61d92f7`, so
four of six points carried the pre-pass kernel. `git log --graph --boundary`
shows that in one screen.

A fourth, smaller one: `weavepack`'s first failure was blamed on contention
*I* had introduced by running other work in its window. Re-running it on a
verified-idle box gave the identical result, so the theory was wrong — but the
re-run is what established that, and it cost sixteen minutes to buy a fact
instead of a guess.

### 9.3 The suite grew underneath it

The tree moved while the soak ran — three other sessions pushed to `elendilon`
— and the soak tier is **207 rows now, not 200**. The eight that arrived
afterwards are theirs, not this pass's, and are recorded here only so nobody
reads "0 not run" as covering them:

* `paint1bpp`, `paint1bpp-colour`, `paint1load`, `paint1load-vga`,
  `paintrz-1bpp` — the 1bpp canvas work. **Run anyway, all five pass.**
* `fcpsmall`, `fdlgsmall`, `stk0water` — `kern_small` rows, **not run, and
  blocked rather than skipped**: `make build/small360.img` fails on the
  branch with `No rule to make target 'build/smallk/filecp.drv'`. That is the
  `kern_small` module split's own in-flight work and NOT this pass's — the
  Makefile in the merge is byte-identical to the one pushed, and this pass's
  whole side of that merge is one documentation file. `small128`, `smallboot`
  and `buildmatrix` fail on the same target. Its shape is the trap the
  Makefile's own comment 8 lines above it describes: `$(SMALLMODS)` is a
  prerequisite in the OUTER make, and the modules are produced by the
  sub-make inside the recipe, so make demands a file before anything can
  create it.

### 9.4 How it was run, if it has to be run again

~3.5 hours of wall clock, and the shape that got it there:

* **`--marty-jobs 3` with THREE rows a chunk**, sorted longest-declared first.
  A chunk then costs `max(row)` rather than `sum(row)` — 6.1 h of declared
  serial time became 2.2 h — and the long rows are behind you early.
* **A `.claim` marker taken before the run and a `.done` written after**, so a
  background runner and a foreground one never pick the same chunk and a
  container restart costs one chunk.
* **One `make` at the top of the runner, and a preflight refusal treated as
  fatal.** `make test-full` leaves a KNOB kernel in `build/`, and
  `os88test`'s symbol preflight then refuses every emulator row — which
  marked two chunks "done" in three seconds each before that guard existed.
* **Verdicts keyed by row with the latest log winning**, so a re-run
  supersedes its own earlier failure with no bookkeeping. `status.py` in the
  scratch directory is twenty lines and was worth writing: counting `.done`
  markers instead had the tally understating by four rows and still listing
  two resolved failures.
