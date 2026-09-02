# `tests/weave/packerr/` — the pack-refusal corpus

One directory per rule, each a minimal Weave project whose entry file is
`MAIN.WML` (uppercase: the message text carries the file name and the
machine's FAT12 spells it that way), plus a `MAIN.WJS`, `SHEET.WFX` or
`SPRITES.WSP` only where the rule needs one. Every project here is valid
right up to the single defect it is built around, so what it refuses on is
the rule named in the table and never something earlier in the pass.

WEAVE-SPEC §10.5 pins the pack-time sentences — `<file>:<line>: <message>`,
"the sentences name the platform fact" — because **two packers print them**:
`tools/weavesim.py --pack` on the host and LOOM.OVL's `File → Pack Bundle`
on the machine (WEAVE-SPEC §11.1). Byte identity of the *output* is the
gate for a project that packs; this corpus is the gate for one that does
not, and a refusal whose wording drifts is a refusal a user cannot look up.
WEAVE-SPEC §12.3 is where the row that diffs the two lives.

Each case is run as

```
python3 tools/weavesim.py --pack tests/weave/packerr/<case>/MAIN.WML -o /dev/null
```

which exits 1 and prints one line on stderr. The sentences below are what
that run printed — captured, never inferred. The message column is the part
after `<file>:<line>: `; the file and line are in the case's own source and
are themselves part of the contract.

**The one row of WEAVE-SPEC §10.5's table with no case here is the oversize
bundle.** The cap is 63,488 bytes and no source under 20KB reaches it: the
two sections with any expansion at all are ATOMS, bounded at 187 atoms of
255 bytes by WEAVE-SPEC §2.7 and needing at least its own bytes of source,
and CELLS, 8-byte records (WEAVE-SPEC §2.10) bounded at 6,140 cells by the
grid cap — 49,120 bytes, off about 52KB of `<cellref> = <n>` lines. The
cheapest bundle over the cap is therefore some 55KB of source, and a fixture
that size buys one sentence. It is left to the demo disks, where a bundle
that grows past the cap is caught by the same check.

**One thing a second packer will get wrong from the table alone**:
WEAVE-SPEC §10.5 illustrates the unknown-attribute rule with
`button: no such attribute "color"`, and no input produces that line — an
attribute whose name *contains* `color` is caught by the colour-vocabulary
rule one branch earlier, so `color="red"` prints the SPEC.md §39.4 sentence
instead. `unknown-attribute/` therefore writes `pad="2"`, which is the same
rule with a name the earlier branch does not claim. The two branches, and
their order, are the contract; the table's example is prose.


## WEAVE-SPEC §10.5's table, row by row

| case | rule | the sentence weavesim prints |
|---|---|---|
| `unknown-element/` | unknown element - the inventory is closed (WEAVE-SPEC §3.2) | `<zap>: not a Weave element; the inventory is closed (WEAVE-SPEC 3.2)` |
| `unknown-attribute/` | unknown attribute (WEAVE-SPEC §3.3) | `button: no such attribute "pad"; style is bold/invert/align only - two of three adapters are 1bpp` |
| `hover-vocabulary/` | hover vocabulary - `onhover` (WEAVE-SPEC §9.1) | `onhover: no hover exists; pointer movement reaches a package only between press and release (SPEC.md 13.7)` |
| `color-vocabulary/` | colour vocabulary on a FLOW component - `color` (WEAVE-SPEC §9.2.1) | `color: no color here; a palette is a canvas's (WEAVE-SPEC 9.2.1) - grey rounds to black on 1bpp (SPEC.md 39.4)` |
| `ontick-over-budget/` | `ontick` over the 64-op budget (WEAVE-SPEC §4.11.1) | `ontick handler is 66 ops; the cap is 64 - per-frame JS does not fit 10-30k ops/s` |
| `too-many-atoms/` | more than 187 app atoms (WEAVE-SPEC §2.7) | `188 app atoms; the cap is 187 - atom ids are one byte` |
| `grid-too-big/` | grid over 6,140 cells (WEAVE-SPEC §5.6) | `grid is 26x256 = 6656 cells; the cap is 6140 - the cell store plus its pool must fit a 26KB claim` |
| `inline-script/` | inline `<script>` body (WEAVE-SPEC §3.2) | `script: inline script is not packed; name a .WJS file - the runtime never parses text` |


## The classes WEAVE-SPEC §10.5's closing paragraph names

| case | rule | the sentence weavesim prints |
|---|---|---|
| `unknown-event/` | unknown event - an event legal somewhere, not on this element (WEAVE-SPEC §3.4) | `label: no event "onclick" exists on it (WEAVE-SPEC 3.3)` |
| `event-unknown-function/` | an event naming a function the script does not define (WEAVE-SPEC §11.3) | `onclick="nope": no such function in the script` |
| `call-argc/` | bad arity - a call with the wrong argument count (WEAVE-SPEC §4.2) | `one: takes 1 arguments; 2 written` |
| `undeclared-identifier/` | an undeclared identifier (WEAVE-SPEC §4.2) | `x: not a local, global, component id or function (WEAVE-SPEC 4.2)` |
| `wjs-locals-cap/` | frame overdepth - over 16 locals in one function (WEAVE-SPEC §4.2) | `f: 17 locals; the cap is 16, parameters included (WEAVE-SPEC 4.2)` |
| `fx-stack-depth/` | stack overdepth - a formula over the 16-slot eval stack (WEAVE-SPEC §5.3) | `formula: too deep - the stack is 16 slots.` |


## Required properties absent (WEAVE-SPEC §3.3, and WEAVE-SPEC §10.4's `required property`)

| case | rule | the sentence weavesim prints |
|---|---|---|
| `radio-missing-group/` | `radio` with no `group` | `radio: attribute "group" is required` |
| `box-missing-wh/` | `box` with no `w`/`h` | `box: w and h are required, 2x1 or more` |
| `grid-missing-cols/` | `grid` with no `cols` | `grid: attribute "cols" is required` |
| `canvas-missing-h/` | `canvas` with no `h` | `canvas: attribute "h" is required` |


## Properties outside their range (WEAVE-SPEC §3.3, and WEAVE-SPEC §10.4's `property range`)

| case | rule | the sentence weavesim prints |
|---|---|---|
| `meter-max-range/` | `meter` `max` outside 1..32000 | `meter: max="40000" is outside 1..32000` |
| `input-cols-range/` | `input` `cols` outside 2..60 | `input: cols="80" is outside 2..60` |
| `list-rows-range/` | `list` `rows` outside 1..40 | `list: rows="50" is outside 1..40` |
| `canvas-w-not-byte/` | `canvas` `w` not a multiple of 8 | `canvas: w="100" is not a multiple of 8 - bands are byte-aligned (WEAVE-SPEC 3.3)` |
| `canvas-color-name/` | a palette name outside the sixteen (WEAVE-SPEC §6.10.7) | `canvas: paper="beige" is not one of the sixteen colours (WEAVE-SPEC 6.10.7)` |
| `canvas-pen-pair/` | a pen pair `GFX_BLIT1` refuses (WEAVE-SPEC §6.10.7, SPEC.md §5.4.2.2's fourth refusal) | `canvas: paper="blue" against color="red": the two share no plane either way and GFX_BLIT1 refuses the pair (SPEC.md 5.4.2.2)` |


## WML vocabulary and syntax (WEAVE-SPEC §3.1, WEAVE-SPEC §3.5)

| case | rule | the sentence weavesim prints |
|---|---|---|
| `bad-style/` | a `style` token outside bold/invert | `style: no such style "italic"; style is bold/invert/align only - two of three adapters are 1bpp` |
| `bad-align/` | an `align` token outside left/center/right | `align: no such alignment "middle"; left/center/right (WEAVE-SPEC 3.3)` |
| `bad-boolean/` | a boolean attribute that is neither bare nor "1"/"0" | `label: br="yes" is not a boolean - bare or "1"/"0" (WEAVE-SPEC 3.1)` |
| `bad-entity/` | an entity outside the closed set of four | `&nbsp: not one of &lt; &gt; &amp; &quot; - the entity set is closed (WEAVE-SPEC 3.1)` |
| `tag-mismatch/` | a close tag that does not nest | `</card> closes <label> - tags must nest (WEAVE-SPEC 3.1)` |
| `tag-unclosed/` | an element never closed | `<card> is never closed` |
| `radio-group-of-one/` | a radio group with one member (WEAVE-SPEC §11.3) | `radio: group "size" has one member; a group is 2 or more` |


## WJS (WEAVE-SPEC §4.1, WEAVE-SPEC §4.2)

| case | rule | the sentence weavesim prints |
|---|---|---|
| `wjs-redeclaration/` | a name declared twice | `a: declared twice` |
| `break-outside-loop/` | `break` outside a loop | `break outside a loop` |
| `number-over-32767/` | a number literal over 32767 | `40000: numbers are 0..32767; 16-bit int is THE number type (WEAVE-SPEC 4.1)` |
| `bad-string-escape/` | a string escape outside `\" \\ \n` | `\q: the escapes are \" \\ \n (WEAVE-SPEC 4.2)` |
| `unterminated-string/` | an unterminated string | `unterminated string` |
| `unterminated-comment/` | an unterminated `/*` comment | `unterminated /* comment` |


## The sidecars (WEAVE-SPEC §3.6, WEAVE-SPEC §5.1, WEAVE-SPEC §5.4)

| case | rule | the sentence weavesim prints |
|---|---|---|
| `wsp-illegal-char/` | a `.WSP` art row carrying a character that is not `#` or `.` | `sprite BALL: a row is exactly 8 of '#' and '.'` |
| `wfx-cell-outside-grid/` | a `.WFX` cell reference outside the grid | `C9 is outside the 3x4 grid` |
| `fx-unknown-function/` | an FX formula naming a function outside the set of eight | `formula: SUM MIN MAX AVG COUNT IF ABS ROUND is the whole set.` |
| `fx-range-outside-aggregate/` | an FX range outside an aggregate's argument | `formula: a range is legal only in an aggregate.` |
| `fx-five-decimals/` | an FX fraction with five decimals | `1.23456: at most 4 fraction digits; 16.16 resolves to 1/65536 (WEAVE-SPEC 5.1)` |

## Two cases wave 6 added

| case directory | the rule it exercises | the sentence |
|---|---|---|
| `atom-empty/` | WEAVE-SPEC §2.7's LOWER bound - `group=""` reaches the interner with nothing in the builder | `empty string: an atom is 1..255 bytes (WEAVE-SPEC 2.7)` |
| `atom-too-long/` | ...and its upper one: an atom's length is ONE BYTE, and the sentence names the length that was WRITTEN rather than the length that fitted | `string is 300 bytes; the cap is 255 (WEAVE-SPEC 2.7)` |

Both were found by writing the second implementation: the empty one because
`ovl_intern()` had no lower-bound check at all and would have pooled a
zero-length row, and the long one because the on-machine builder stops at 255
and had to be taught to keep counting so that its sentence could name the same
number the host's does.

## The FX sentences are the RESIDENT compiler's

Three rows above read differently from the way `tools/weavesim.py` first
wrote them, and the change is a decision rather than a drift. LOOM's FX
pre-compiler **is** `apps/weave/wfxc.c` - `#include`d, not rewritten, because
WEAVE-SPEC 1.2 says what the two packages share they share as source and
because that file's own header says two grammars for one language is the
drift WEAVE-SPEC 11's byte-identity rule exists to prevent. A shared compiler
has one vocabulary by construction, so `weavesim`'s `FxCompiler` was moved
onto WEAVE-SPEC 6.9.2's sentences rather than the other way round: the
formula bar's wording is what a person already sees when a formula will not
compile in a running app, and the family now says the same thing whether the
formula was typed into a cell or packed out of a `.WFX`.

What it costs is named too: the pack-time sentence no longer quotes the
offending function's name, because the resident compiler's string literals
are resident bytes and WEAVE closed wave 5 with 32 of them spare
(WEAVE-SPEC 13.1). WEAVE-SPEC 10.5 carries the rule.
