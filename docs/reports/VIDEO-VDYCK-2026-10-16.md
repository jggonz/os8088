# The yield site as a 3-byte patched call - built, measured, NOT taken (2026-10-16)

**Taken on:** commit `928763ad` (the change), against
`2c6edafc` (its base, elendilon-next after the second video size pass). Box: a
cloud container, MartyPC's 5150 (`os8088_5150_*`, cycle-exact 4.77 MHz 8088).
286 figures are PREDICTED from Intel's 80286 timing table - nothing here can run
a 286 - against the Victory 286's measured decode times in
`docs/reports/VIDPM-VICTORY286-2026-10-15.md`.

**Decision (the owner, 2026-10-16): not taken.** The trade is a faster 8088 and
258 bytes against a slower 286 decoding in place; the owner keeps the 286's
in-place play as it is. **The change itself is kept beside this report, as
`VIDEO-VDYCK-2026-10-16.patch`** (`git format-patch` of `928763ad`: the decoder,
the player, SPEC.md 98.3.18.11, LOADALL-PLAN 9.2 and `tests/vidxip.py`'s
updated check). It applies with `git am` to `2c6edafc`; on a later tree expect
to redo `VD_PAR`'s pads and the hot parity, which is where its work was.

## What it was

LOADALL-PLAN 9.2's option 1. A `VD_YCK` yield site was twelve bytes: a jump over
itself (`EB 0A`) and an inline `cmp si, [cs:vd_ylim] / jb / call vd_yield`, an
in-place session patching the jump to two NOPs. The change made a site three
bytes, `test ax, rel16` (`A9`), whose immediate is already the near-call
displacement to `vd_yield`; `vd_ypatch` flips the one byte to `E8` to arm it and
back to `A9` at the session's end, and `vd_yield` makes the compare itself. The
C160 decoder's eight sites went too: they were never in `vd_ytab`, and C160 is
decoded only through the shadow, which neither in-place path takes.

**Bytes: -258 in every arm** (default 53,646 -> 53,388, `NOLIVESND` 53,417 ->
53,159, `VPDIAG` 55,006 -> 54,748; bss unchanged).

## 8088, MEASURED on MartyPC's 5150

~6 to 7 cycles a site visit (a taken jump and its queue flush, against a `test`
that falls through), 35-90 cycles a frame, 0.1-0.4% of a decode.

| VIDBENCH frame | to screen | to RAM |
|---|---:|---:|
| empty | 1,441 -> 1,387 (-54) | |
| P1 x400 | -74 | -46 |
| SL16 | -56 | -61 |
| RUN16 | -90 | -67 |
| the rest | -35 to -79 | |

`vidfskeys --cost`, hook a frame, text down: Hercules 9,363 -> 9,294, CGA4
9,086 -> 9,040, VGA8 15,550 -> 15,482, VGA4 write mode 3 41,217 -> 40,407,
Mode X flipping 19,536 -> 19,421. Text up: within noise on every kind.

Site visits a frame were counted exactly on the host from the files' own lists
and cross-checked on MartyPC (every site armed as a call to a bare `ret` costs a
steady 40 +-1 cycles a visit at the host's counts): 8-10 on the synthetic
frames, 9.0 to 38.1 on average on the demo files, 82 at most.

## 286 decoding in place, PREDICTED

| form | clocks a site visit |
|---|---:|
| before: NOP 3 + NOP 3 + CMP 6 + taken JB 7+m | 20 (24 at a poke head) |
| after: CALL 7+m + CMP 6 + JAE not taken 3 + RET 11+m | 33 (37) |
| **difference** | **+13** |

That is 7-31 us a frame at 16 MHz on the demo files (~67 us at most), or as a
share of the Victory's measured decode: SL/RUN frames +0.25-0.27%, P4 x300
+0.34%, P1 x400 +0.63%, P1 row +2.3%, P1 sparse +5.6%, the empty frame +15%
(6.5 us). An in-place frame also pays its borrows (~150 us each), so the shares
overstate the loss; a 640x400 VGA4 file at ~17 ms a frame is estimated at 0.7%
or less (visits on a VGA4 file were not counted). A 286 that COPIES rather than
decoding in place would have gained ~9 clocks a site.

No variant avoids the 286 cost cheaply: keeping the inline form at the four
span-head sites (+36 bytes) removes 33-84% of the loss and gives back the same
share of the 8088's gain, and a second decoder for in-place play stays refused
(LOADALL-PLAN 9.2 option 3).
