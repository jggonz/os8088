# What the shared SDK includes cost, and the merge that is refused

The kernel size passes cover `kernel/` and nothing else. `apps/os88*.inc` are
the includes every package compiles in, and no size pass has touched them. This
is the measurement of what they actually cost, and one refusal with the
arithmetic attached so nobody re-derives it.

**Everything here is assembled, never estimated.** `tools/incsize.py` is the
instrument — `kernsize.py --modules` for a package — and every number below is
reproducible with it.

---

## 1. The ranking by source size is upside down

The obvious way to rank the shared includes is source bytes times includers.
Done that way `apps/os88api.inc` leads by an order of magnitude: 229,207 bytes
in ninety packages.

**It emits nothing.** Measured: 0 code lines, 0 labels, 226 `equ`s, 155
`%define`s and 13 macros. A package pays for what it *uses*, so there is
nothing in it to shrink. The same is true in lesser degree of the other
`equ`-heavy includes.

What actually emits, and what it costs a package that includes it:

| include | code lines | labels | assembled into notepad |
|---|---:|---:|---:|
| `apps/os88ui.inc` | 1,009 | 164 | **2,364 bytes** |
| `apps/os88type.inc` | 972 | 125 | (only 4 includers) |
| `apps/os88api.inc` | 0 | 0 | **0** |

`os88ui.inc` varies by package because three of its regions are optional —
2,364 bytes in notepad, 2,384 in SHEET, 1,448 in Paint.

Its weight is concentrated: `os88ui_btn` (277) and `os88ui_glyph` (271) are
23% of it between them, and both are already tight. `os88ui_glyph` computes
`i * 24` as `i * 8 + i * 16` because the 8086 has no shift by an immediate
other than 1 and `CL` is live; a four-entry lookup table costs 17 bytes against
its 14 and loses.

---

## 2. Priced and refused: merging the seven identical epilogues

**The finding.** Seven routines in `os88ui.inc` end with byte-identical
epilogues — `pop di / pop si / pop dx / pop cx / pop bx / pop ax / ret`, 7 bytes
each, 49 in total:

```
os88ui_btn  os88ui_glyph  os88ui_sbar  os88ui_sbmove
os88ui_ask  os88ui_abtn1  os88ui_apaint
```

Tail-merging them into one shared exit measured **−25 bytes in notepad** and
**−17 in Paint**.

**Why it is refused.** Those seven live in three *independent optional feature
regions*, and `os88ui.inc` has **no unconditional code stretch anywhere** for a
shared tail to live in:

| region | routines |
|---|---|
| `%ifndef OS88UI_BARONLY` | `os88ui_btn`, `os88ui_glyph` |
| `%ifdef OS88UI_SCROLL` | `os88ui_sbar`, `os88ui_sbmove` |
| `%ifdef OS88UI_ALERT` | `os88ui_ask`, `os88ui_abtn1`, `os88ui_apaint` |

The −25 measurement came from putting the shared tail in the `ALERT` region and
jumping to it from the other two, which **does not build**: `artful` enables
none of the three and `nasm` refused with `symbol 'os88ui_x6' not defined`. The
number was real and the change was not.

The arithmetic on the two ways out:

* **An unconditional tail** costs 7 bytes to *every* includer and saves 4 bytes
  a site. Of the 18 packages that include `os88ui.inc`, **3** enable both
  `SCROLL` and `ALERT` without `BARONLY`. So it pays 21 bytes back to three
  packages and charges 7 to fifteen — **a net loss across the tree**, before
  counting that the three feature regions were made optional precisely so a
  package would not carry what it does not use.
* **Merging within each region** never loses, and never wins much: 4–5 bytes for
  the two-site regions and 8–10 for the three-site one, so **≤16 bytes** and
  only for a package enabling all three. Against that: six of the seven
  routines stop ending in a visible `ret`, in the SDK header that package
  authors read.

Refused on the second, not the first — 16 bytes is real, and it is not worth
making seven exits indirect on a published surface. **If it is ever taken, take
it per region; the cross-region version cannot be built.**

---

## 3. Not taken, briefly

* **Prologue sharing.** 29 routines open with 103 bytes of `push`, the commonest
  shapes being `ax bx cx dx si di bp` (×4) and `ax bx cx dx si di` (×3). A
  helper cannot do it: a `call` puts its return address in the middle of the
  saved set, and unpicking that costs more than the pushes.
* **The repeated rect load.** `mov ax,[ds:bp+0]` … `+6` appears exactly twice,
  in `os88ui_btn`. A helper is 23 bytes against 32 — 9 bytes, one routine, and
  it puts a `call` between the fill and the frame.
* **`UI_*` macro bodies.** The heaviest is used 5 times. Nothing there.

---

## 4. Method

```sh
python3 tools/incsize.py apps/os88ui.inc apps/notepad/notepad.asm
```

Attribution is by **address**, out of a `nasm -l` listing, so it is what the
assembler emitted — jump distances included — rather than instruction bytes
counted by hand. For a whole-tree before/after, `make` and diff the sizes of
`build/*.bin`; that is the only measurement that catches a change which shrinks
one package and grows another.

**And build every package before believing a number.** The refused merge above
measured a genuine −25 bytes in the one package it was tried on and did not
assemble in a package that turns those features off. `make` builds them all.
