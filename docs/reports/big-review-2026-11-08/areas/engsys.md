# LENS: the kernel engineering system - guards, ratchets, SPEC, tests

Reviewer: engsys lens of the 2026-11-08 blue-sky review, on `the-big-review`
at `4f0da91` (a shallow clone; nothing here depends on ancestry). Read-only:
nothing was built, no row was run. Every count below was taken with `grep`,
`git ls-files` or a short Python read of the tree, and says how.

## Scope read

| what | lines | how much |
|---|---:|---|
| `kernel/kernel.asm` | 9,886 | the budget block (1404-1520), the section declarations (2142-2270), the fixed low cells (2321-2393), the API table's guard (4488-4496), the MODFP block (7310-7366), the size guards and their `%warning ks:` facts line (9380-9720) and the end-of-file section sentinels (9760-9886), whole |
| `tools/checkdocs.py` | 367 | whole |
| `tools/os88index.py` | 846 | header and GROUPS (1-80) |
| `tests/suite.py` | 21,819 | the docstring and `Row` (1-200), the `full` rows (2405-2465), `brcapacity-fixtures`; the whole registry loaded through `suite.rows()` for every count |
| `tools/os88test.py` | 1,497 | the budget, `charge`, the lane split, the soak refusal (600-680, 1315-1495) |
| `tools/os88soak.py` | 1,573 | the refusal (1040-1060) |
| `tools/abilock.py`, `tools/abi.lock.json` | 207 | the header, GUARDS, `surface()` |
| `tests/unit/` | 37,543 in 146 files | read whole: `harness.py`, `t_kernbudget.py`, `t_registry.py` (1-120), `t_api_abi.py` (1-60), `t_mirror.py` (1-50, 165-300), `t_textrules.py` (120-160), `t_movable.py` (280-300), `t_stkbalance.py` (1-40), `t_asmrules.py` rule list |
| `Makefile` | 20,872 | structure: the header, `all`, `test-fast`, `checkdocs`, `budget-check`, the `#>` markers |
| `SPEC.md` | 245,112 | as a document: all 129 top-level headings, the preamble and §1 (1-168), §3, §4, §15.1, §16, §17, §20.3 (44299-44480, 44795-44830), §33, §47's head, §62.9.1's block; plus a scripted pass over every numbered heading and every fenced `equ` line |
| `docs/README.md`, `docs/INDEX.md` (head), `docs/TESTING.md` (193-492), `docs/WRITING-TESTS.md` (2.1, 2.2, 3), `CLAUDE.md`/`AGENTS.md` (diffed) | | as stated |
| `docs/plans/ABI-BREAK-LIST.md`, `docs/plans/KERNEL-BYTE-QUEUE.md`, `docs/reports/CODE-REVIEW-TWO-CYCLES-2026-11-01.md` | | for what is already listed |

## Verdict

The engineering system is the best thing about this tree that nobody sees.
The kernel carries 636 assembly-time assertions (114 in `kernel.asm`, 522 in
the `.inc` files); budgets are DERIVED from a stated rule rather than chosen;
the API table is checked against the bytes the assembler emitted, not against
the source; the SDK is fingerprinted and every bump carries a written reason;
and a dozen hard rules that used to be prose (toast length, transparent text,
`.vga` doors, module drops, movable claims, mega-only code) are now gates.
What is weak is the half that is still prose, and it is weak in one
recognisable way: **a statement written as a running count, a hand copy of
code, or a section number that resolves**. Those are the three shapes that
drift, the gates cover none of them, and the review found each one wrong in
the parts of SPEC.md a newcomer reads first (§1, §3, §4, §15-§17, §20.3).
`checkdocs` proves that a § number EXISTS; six citations of the greying
standard name ArtfulType's section and pass. The fast tier's admission rules
are written down, are good, and are broken by about seventeen rows, because
admission is policed only by a CPU budget. None of the fixes costs a kernel
byte; most are a few dozen lines in an existing gate.

## Strengths

- **The assembler is the first gate.** 636 `%error` sites, including ones that
  enforce *content* rules at the site that breaks them: `TOASTSTR`'s length
  (kernel.asm:345-366), `COLDCALL`/`TEXTCALL`/`KDATAR` refusing to assemble
  outside their section via `__?SECT?__` (kernel.asm:5524-5546), `MODFP`
  refusing a module block out of `MOD_*` order or with zero entries
  (kernel.asm:7321-7327, 7361-7363), `osapi_sys_kb`'s parts required to SUM to
  what they measure (guard 7, kernel.asm:9561-9588) so the Task Manager's
  numbers cannot lie. This is the GEOS Esp idea (an assembler that knows the
  conventions) done with NASM's preprocessor. Keep copying it.
- **Budgets are derived, not chosen.** `KERN_BUDGET equ
  KERN_RESIDENT_KB*1024 - KERNEL_SEG*16` (kernel.asm:1446-1455), so the only
  decision is the KB figure; `KERN_KNOB` takes knob builds out of guard 1
  and states why (kernel.asm:9447-9468); `budget-check` ends the grace on all
  four budgeted kernels (Makefile:14825-14831).
- **Facts flow out of the assembler.** `%warning ks: text=... budget=...
  grace=...` (kernel.asm:9425) hands `tools/kernsize.py` the ladder the kernel
  HAS instead of a Python re-derivation; `tools/os88sym.py` re-assembles and
  refuses unless byte-identical. The right model; see engsys-14 for where it is
  not yet used.
- **`t_api_abi` reads the binary.** It decodes every cell shape out of
  `build/kernel.bin` and checks each SDK address lands on a cell whose routine
  is the one the name promises, inline cells included (t_api_abi.py:1-60). Of
  78 `OSAPI_X ... 0x0NNN` name/number pairs in SPEC.md, CLAUDE.md,
  KERNEL-MEMORY.md, TESTING.md and the SDK, all 78 agree today.
- **The ABI lock makes a bump a sentence.** `tools/abi.lock.json` holds the
  reason for every fingerprint move; the fingerprint reads code and the
  indented contract comment under each `OSAPI_*` %define and nothing else
  (abilock.py docstring). Cheaper and more honest than Windows' "ordinals never
  change" or Mac's trap-number freeze, for a tree that rebuilds every caller.
- **Self-maintaining mirrors.** `t_mirror` takes every `NAME equ VALUE` from
  every kernel file and the SDK and compares any name defined twice, so a
  newly mirrored constant is covered with nobody remembering (t_mirror.py:24-
  33). The lesson it records - a self-maintaining check with a hand-written
  half is a hand-written check - is exactly engsys-11.
- **`checkdocs` learned from its own misses**: duplicate headings (a set hid
  them), three-digit sections, document-qualified citations, committed
  conflict markers, the transparent-text totals with a table of HISTORICAL
  pairs. `os88index.py --check` makes the "does this exist?" index a build
  artefact, which nobody else in this era had.
- **The harness is tiny and right**: `check()` never raises, prints want/got
  and the first differing offset, and `why=` is mandatory culture
  (harness.py:1-76). Gate failures here are actionable a year later.
- **Tier policy is well argued.** "Is it worth having to somebody who did not
  touch this?" (WRITING-TESTS §2.1) and "did you obviously break the OS?"
  (§2.2) are the right questions; CPU-charged budgets instead of wall clock
  (os88test.py:614-650) stop a busy box failing a green tier.

## Findings

Ranked by value. Bytes are kernel bytes; every recommendation here is
host-side tooling or text, so every figure is 0 resident, 0 module.

### engsys-1 (bug, high, now) - this branch's own reports fail `make`

`python3 tools/checkdocs.py` on `4f0da91` reports 6 problems, all in committed
review reports: `areas/gfx.md:22`, `areas/input.md:113`, `areas/memory.md:132`,
`areas/ui.md:16` (two), `areas/video.md:773`. `checkdocs` is `all`'s FIRST
prerequisite (Makefile:2425), so `make` on `the-big-review` stops before a
floppy is built. Several are reports QUOTING a failing citation or a stale
pair as evidence - which is what a review does.

Systemic half: docs/README.md says `docs/reports/` "is not maintained and is
not meant to be", and `docs/history/` is "true of no tree", yet `checkdocs`
holds both to today's heading map. `SITES_HIST` (checkdocs.py:132-152) is the
cost of that contradiction, paid one pair at a time.

Recommendation: fix the six lines before anything else lands on this branch
(write a quoted bad citation without its `§`/`SPEC.md ` prefix). Then make
`checkdocs` report problems in `docs/reports/`, `docs/history/` and
`docs/plans/completed/` as `warn:` lines (the runner already shows those on a
green row, os88test.py:1438-1446) and keep failing everywhere else. Bytes 0.

### engsys-2 (hazard, high, now) - a citation that RESOLVES to the wrong section passes

The preamble (SPEC.md:20-23) says renumbering is refused because
`checkdocs` "would only catch the ones that stopped resolving - not the ones
that resolved to something else". That class is live today without any
renumbering: the greying standard is §47 (SPEC.md:96478), and six SPEC.md
lines cite it as §46, which is ArtfulType - SPEC.md:161 (§1 hard rule 8, the
rule that BINDS packages), 26453, 26463 (`§46.3`, which exists - ArtfulType's
layout), 26555 (`§46 rule 5`), 55757, 76640 (`§46 rule 1`). All six pass.

Recommendation, two parts:
1. Fix the six (doc mismatches below).
2. A **heading-drift check** in `checkdocs`: read SPEC.md's `{number: title}`
   map at `git merge-base HEAD origin/elendilon` (one `git show`) and fail
   when a number's title changed or a title's number changed between base and
   HEAD, naming every citation of the old number. That turns the preamble's
   "cannot be caught" into a gate for every future renumber and every
   insert-in-the-middle. Optional third: where a citation is followed by a
   parenthesised word (`§47 (greying)`), check the word against the title.
   Prior art: Inside Macintosh and the NT design workbook cited by chapter
   NAME, which cannot resolve to the wrong thing. Bytes 0.

### engsys-3 (weakness, high, now) - SPEC hand-copies of code, ungated, wrong

SPEC.md carries code in fences that nothing compares with the code:
- §3 "Global constants (defined once in kernel.asm)" (SPEC.md:4040-4086):
  `KERNEL_SEG equ 0x0800` (it is 0x0060, kernel.asm:27), `KERN_BUDGET equ
  74240` (derived, 100,864 / 64,000 / 68,096, kernel.asm:1446-1455),
  `STK0_SIZE equ 1024` (512, kernel.asm:1568), `VIEW_KB equ 3` (1 or 2,
  kernel.asm:1622/1628).
- §15.1 "Size guards" (SPEC.md:32304-32350) reproduces guard 1 as `%if
  KERN_SIZE > KERN_BUDGET` (the code adds `KERN_GRACE`, a kern_mega arm and the
  `KERN_KNOB` skip, kernel.asm:9469-9477) and guard 5 without `BOOT2_PAD`
  (kernel.asm:9682), and prices the budget "against 86,528 bytes"
  (SPEC.md:32358).
- §62.9.1: "`DSV_SIZE` is 26" and `DSV_SIZE equ 28` (SPEC.md:113326, 113335);
  it is 36 (kernel/driver.inc:557, drivers/os88drv.inc:410).

Measured: of the 182 literal `NAME equ N` lines inside SPEC.md code fences
whose NAME the kernel, the SDK or `os88drv.inc` defines, **4 disagree**
(KERNEL_SEG, STK0_SIZE, VIEW_KB, DSV_SIZE). A gate would be nearly clean on day
one.

Recommendation: teach `t_mirror` (it already parses `equ` everywhere) to read
SPEC.md's fenced `nasm` blocks, comparing a literal against the set of values
the tree defines for that name; a block under a heading containing
"retired"/"used to" or preceded by `<!-- history -->` is skipped. Better for
§3 and §15.1: replace both with GENERATED blocks between markers, the way
`docs/KERNEL-MEMORY.md`'s kernsize baseline is (t_kernbudget.py), so the
prose explains and the tree supplies the numbers. Bytes 0.

### engsys-4 (weakness, high, now) - the front matter describes retired designs as current

The sections a newcomer meets first are the stalest:
- §1 rule 2 (SPEC.md:102-107): "CS = DS = `KERNEL_SEG` (0x0800)", "`LOW_SEG`
  (0x0440)" (computed, kernel.asm:1798), and a garbled sentence ending "there
  is no far code and no second code segment (§33)". `.cold` is a second code
  segment reached far (`COLD_SEG`, `COLDCALL`, 363 far-call sites across
  `kernel/`), and `.vga`/`.mega` are more.
- §1 rule 4 (SPEC.md:128-134): "Two further sections exist ... All four are
  declared once" - kernel.asm:2142-2269 declares about thirty; "the final
  `section .bss` block (§15) is the last thing assembled" - the last thing is
  the sentinel block (kernel.asm:9786-9886), which says it MUST be.
- §33 (SPEC.md:70971-70991) "Far code - retired ... Cold code is ordinary code,
  near called like everything else" - contradicts §2.6 and CLAUDE.md's own
  `.cold` rule. The arithmetic §33 tells (footprint, not window) is still
  true; its conclusion is not.
- §4 (SPEC.md:4090-4132): "boot splash entry at 0x0008" (0x08-0x0B are FREE,
  kernel.asm:2359-2365) and "The table is the whole of `kernel/`, and that is
  now literally true" - **35 of the 74 files in `kernel/` are not in it**
  (`assoc.inc`, `clip.inc`, `toast.inc`, `cpudet.inc`, `driver.inc`, `mod.inc`,
  `xmem.inc`, `lz.inc`, `blank.inc`, the eight `mega*`/`vx*`/`vmm*` files ...).
- §16 (SPEC.md:34124-34210): "`make bench` builds both onto
  `build/bench.img`" (no such target), "all three bootable floppies", "the
  kernel may exceed 8 sectors ... Nothing to change in boot.asm".
- §17 Definition of done (SPEC.md:34211-34300): "menu bar (logo + File +
  Special)", "two Timers tick" (Timer is a package, §111), "segment 0x0800",
  QEMU as the instrument.

Recommendation: rewrite §1-§4 and §15-§17 as a short CURRENT front matter
(each rule one paragraph and the gate that enforces it), and move the retired
narrative to `docs/history/`. Gate §4: a fast check that every `kernel/*.inc`
appears in §4's table (CLAUDE.md:953 already promises it). Prior art: Inside
Macintosh volumes IV-VI carried "this supersedes Volume I, chapter N" banners
rather than rewriting history into the current chapter; here the history is
inline and reads as the contract. Bytes 0.

### engsys-5 (weakness, medium, now) - two copies of the slot contracts, and they disagree both ways

`apps/os88api.inc` is "the address map" and its per-`%define` contract
comments are what `abilock` fingerprints. SPEC.md §20.3 carries a second copy,
"Slot-specific contracts that are not simply their target routine's"
(SPEC.md:44800-44960, 37 entries), which no tool reads. They disagree:
- `OSAPI_SND_PLAY`: SPEC still gives the blocking-clip contract
  (SPEC.md:44810-44816); the SDK and code are the GRANT/RELEASE door
  (os88api.inc:539-545). Listed: ABI-BREAK-LIST §11 bullet 1 - still open.
- `OSAPI_FULLSCREEN`: here the SDK is the stale one. os88api.inc:892-897 says
  "AL = 1 enter (BX=win ptr) / 0 exit; ... out CF=1 enter refused"; the code
  binds BX on exit and refuses EITHER verb when another window holds the screen
  (kernel/wm.inc:4287-4304, 4306-4313). SPEC §20.3's copy is right.

Recommendation: one copy. The SDK is the one a tool reads and the one a package
author has open, so §20.3's block is deleted (or generated from the SDK by
`os88index.py`), and the FULLSCREEN SDK comment is corrected with `abilock.py
--update` and no bump (a contract clarified). Prior art: Windows 3.x and OS/2
treated `windows.h`/the `.INC` as the reference and the printed manual as its
rendering; Inside Macintosh's per-chapter Summary was the one listing.
Bytes 0.

### engsys-6 (weakness, medium, now) - running counts in contract prose

Every count that describes a set the code defines has gone stale somewhere:
- SPEC §20.3's byte ledger ends "**1,253 bytes**, and `osapi_table_end` is
  `0x04F5`" (SPEC.md:44378). `OSAPI_VOL_CHECK` sits at 0x04F5 (os88api.inc:2567),
  the 119th rare cell; the table is 0x04EB = 1,259 bytes and ends at 0x04FB
  (kernel.asm:4494-4495).
- "The fifteen [inline cells]" (SPEC.md:44411) heads a table of 19 rows; the
  guard counts 17 ICELLs.
- kernel.asm:9787 "TWELVE LABELS AND NOT TWO" - the sentinel block has 20.
- docs/TESTING.md:198 "37 rows, ~13s", :241 "43 rows", :242 "10 rows",
  :243 "The other 907 gates"; WRITING-TESTS.md:138 "The five rows" plus five
  more; Makefile:2547 "(~2s)". `suite.rows()` today: **fast 76 rows declaring
  56.9s, full 14, soak 1,944** (1,595 native plus 349 `mega-`/`box-` ports).
- CLAUDE.md:632 "it is 6,553 lines" (PERFORMANCE.md is 15,235);
  tools/os88index.py:5 "SPEC.md is ~67,000 lines" (245,112); Makefile:22
  "`all` is 1,800 lines down" (line 2425).

`checkdocs` already has the right mechanism for exactly ONE count - the
transparent-text total with `SITES_HIST` (checkdocs.py:124-152).

Recommendation: generalise it into a `COUNTS` table - (regex, a function that
computes the live value, historical pairs with their moment). First entries:
tier row counts from `suite.rows()`, the API table's length and end from
`kernel.map`, inline-cell count, kernel file count. And a convention for the
rest: a count in prose carries its moment ("at kernel size pass 4") or is not
written. The `OSAPI_TABLE_LEN` guard (kernel.asm:4494) is a hand tally that
every new cell edits twice (the arithmetic and the message) and that
`t_api_abi` already supersedes by walking decoded lengths; derive it or drop it
- SPEC.md:44322 already says "its span is whatever the cells add up to".
Bytes 0.

### engsys-7 (weakness, medium, now) - tier admission is prose, and the prose is broken

WRITING-TESTS §2.1: a `fast` row "about ONE package, driver or host tool" goes
to `soak`, and "Being a build input ... is not an exception"; two named owner
exceptions, `kernbudget` and `readme8088`. `fast` today carries WIN16's five
(`w16xsec` 3.0s, `w16wrap` 3.0s, `w16fp` 1.5s, `w16cens` 1.0s, `win16ne`),
`sheetfmtlib` (1.5s), `isotronclean`, `lemdat`, `wrsim`, `x3xttab`, `stik`,
`stikcard`, `romtool`, `catdisk`, `installabi`, `brcapacity-fixtures`,
`arcprebuilt-stamp` (which CLAUDE.md names as deliberate, but WRITING-TESTS
does not list) - about 17 rows and ~14 declared seconds - plus `chromesites`,
which is kernel-internal (rule 2). `full`: WRITING-TESTS §2.2 lists 10 rows;
there are 14, and `w16censall` (one package) and `knobself` (a tool's own
selfcheck) break §2.2's first and third rules. The suite.py docstring's four
families (suite.py:117-133) name none of these.

The only enforcement is the 30s CPU charge, which a four-lane box absorbs, so
the tier has grown from 37 rows to 76 without anything saying so.

Recommendation: `Row` gains `family=` (`boundary`, `rules`, `artifacts`,
`tree`) or `owner=` ("the owner's word, <date>"); `t_registry` fails any fast
or full row with neither, and the family tables in suite.py, TESTING.md and
WRITING-TESTS are generated from it. Then the owner decides row by row: move
it, or record the exception. Bytes 0.

### engsys-8 (hazard, medium, now) - basename collisions silently disable citation checks

docs/README.md:74 says "two documents may not share a basename: the resolver
keeps one and silently checks every citation of the other against it". It is
not gated, and 26 basenames are shared (README.md 27 times, BRIEF.md 11,
SKILL.md 10, LESSONS.md 8, PROMPT.md 4, files.md 6 ...). `stems` is a dict
comprehension (checkdocs.py:204), so the LAST path wins. Concrete false pass:
`LESSONS.md §3` at `.claude/skills/refresh-stale-pr/SKILL.md:181` and
`review-fork-pr/SKILL.md:149,206` resolves to `.claude/skills/vga-face/LESSONS.md`,
which numbers no headings, so the citation is skipped as uncheckable - while
the file it means numbers six.

Recommendation: resolve a qualifier first against the citing file's own
directory, then by basename only when the basename is unique; an ambiguous
basename used as a qualifier fails, naming the candidates. Delete the README
rule once the resolver no longer needs it. Bytes 0.

### engsys-9 (weakness, medium, now) - checkdocs does not read C, headers or the ratchet files

The scan is `.md`, `.inc`, `.asm`, `.py` and the Makefile (checkdocs.py:214-216).
Unchecked: 1,579 citations in `.c`, 138 in `.h`, 205 in `.txt` (including the
reasons in `tests/*.txt`, which ARE the ratchets' record), 185 in `.json`, 63
in `.toml`. Known broken: `apps/paccman/pmc_menu.c:121` cites SPEC.md section
91.14, and §91 has no subsections at all. And `apps/c64/*.c` writes 32 bare `§`
citations meaning C64-SPEC sections, against the tree's rule that a bare `§`
means SPEC.md (CLAUDE.md map, docs/README.md "What does not move").

Recommendation: add `c`, `h`, `txt`, `sh` to the scan, qualify the C64 ones
(`C64-SPEC §`), fix pmc_menu.c. The gate's literal-first skip keeps the cost
near zero. Bytes 0.

### engsys-10 (weakness, medium, now) - AGENTS.md is not the copy CLAUDE.md says it is

CLAUDE.md:5 and AGENTS.md:5: "carry the same repository guidance ... only
their titles and introductory agent descriptions differ". Past the first 12
lines `diff` reports 726 differing lines; AGENTS.md is 786 lines to CLAUDE.md's
1,370 (both last touched in one commit, aab5915). A Codex session reads the
shorter, older rules - e.g. AGENTS.md:666 still says only that §4 is the table,
and its TESTING.md row lacks the tier authority sentence.

Recommendation: generate AGENTS.md from CLAUDE.md with a small substitution
table for the intro and the host's tool names, and check it in `checkdocs`
like the index. A drifted mirror is worse than none, because it is believed.
Bytes 0.

### engsys-11 (weakness, medium, now) - the C SDK's constants and thunks are outside both mirrors

`t_mirror`'s C list is four emulator files (t_mirror.py:181-183) and not
`apps/cc/os88.h`, whose names carry an `OS88_` prefix anyway, so the
name-keyed comparison could not pair them. `abilock` fingerprints
`apps/os88api.inc` and `drivers/os88drv.inc` (abilock.py GUARDS), not
`os88.h` or `os88thunk.asm` - the C packages' half of the same ABI. Measured:
os88.h has 80 numeric `#define`s; 57 pair with an SDK name once the prefix is
stripped, and all 57 agree today.

Recommendation: add os88.h to t_mirror with an `OS88_` -> `` / `OSAPI_` prefix
map, and add os88.h and os88thunk.asm to `PKG_FMT`'s fingerprint files. Prior
art: Microsoft's H2INC generated MASM includes from the C headers so the two
could not disagree; here generating one from the other is also possible, but
the check is the cheaper first step. Bytes 0.

### engsys-12 (hazard, low, now) - section switch-back is gated only for nobits

Hard rule 4 is enforced by `-w+error` only when the left-open section is
nobits. A progbits section left open puts the next include's code in it
silently. One file does it today, by design: `kernel/shutdown.inc` ends in
`.modu` because `ctrl.inc` includes it inside `.modu` (ctrl.inc:1038). So the
real rule is "a file leaves the section it was entered in".

Recommendation: a host check that every `kernel/*.inc`'s last `section`
directive equals the section active at its `%include` (or `.text`), with
`shutdown.inc` registered. Recorded alongside: the end-of-file sentinel block
covers 20 sections but not `.modx`, `.mods`, `.modfs`, `.vga` or `.cfx`; where
the next section's `start=` is this one's exact end, NASM itself refuses the
spill ("fatal: sections .a and .b overlap!", verified on NASM in this
container), so only sections followed by a padded gap need a sentinel - which
is the block's own lesson (`.text` -> `.cold`). Bytes 0.

### engsys-13 (weakness, low, now) - the ratchets are exact registers, not one-way

21 registry files under `tests/`, each with its own parser in its own gate.
The gates force registry == tree in both directions (t_textrules.py:138-150,
t_movable.py:285-300), which is good - but a NEW line raising a count passes
with any reason. "Only turns one way" is held by review.

Recommendation: one shared `tests/unit/ratchet.py` (parse `count path #
reason`, require a reason, compare with the tree), and a direction check that
prints a `warn:` line in the fast tier when a registry's total rose against
the merge-base. A candidate new ratchet while there: the 27 bare `sti` in
`kernel/` against hard rule 3 (most are legitimate - `gfx_lock` documents IF =
1 on return, vga12.inc:5071-5079 - which is exactly what a registry reason is
for). Bytes 0.

### engsys-14 (weakness, low, now) - gates re-derive facts by regexing NASM source

`t_kernbudget` finds `KERN_BUDGET` by POSITION among three `%ifdef` arms and
evaluates one accepted expression shape (t_kernbudget.py:72-98), while the
same build already printed `budget=` on the `ks:` line (kernel.asm:9425). Every
such gate breaks on a reformat rather than on a semantic change.

Recommendation: prefer the assembler's own output - the `ks:`/`ksm:` lines,
`kernel.map`, or a second `%warning` facts line - for anything derived, and
keep source regexes for rules ABOUT source (textrules, toast). Bytes 0.

### engsys-15 (hazard, low, now) - the whole-soak refusal is bypassed by `-k '*'`

Both runners refuse only `not a.k` (os88test.py:1318, os88soak.py:1050), so
`-k '*'` runs all 1,944 rows without `--user-asked`. The rule it carries is
the owner's permission, not a judgement.

Recommendation: refuse when the selection covers, say, over 90% of the tier,
with the same message. Bytes 0.

### engsys-16 (weakness, low, now) - `builds=` is judged per script, not per mode

`brcapacity-fixtures` is a fast row that runs no `make`, and carries
`builds=True` "only because the registry reads tests/brcapacity.py for `make`"
(suite.py:21273-21278) - so it leaves the parallel lane and its flag says
something false. It is also a one-package row (engsys-7).

Recommendation: move it to soak, or let `t_registry` key the `make` test on
the row's argv mode. Bytes 0.

### engsys-17 (weakness, low, record) - guards are cited by an unstable number

Guard numbers run 1, 2, 2d, 3, 3b, 4, 6, 7, 6b, 4b, 5, 5b, 5c in file order
(kernel.asm:9438-9715), SPEC §3 speaks of a "guard 8", and docs cite "guard 1"
and "guard 5". Recommendation: give each a stable tag in its `%error` text
(`[G-BUDGET]`, `[G-BOOT]`) and cite tags. Bytes 0.

### engsys-18 (rethink, medium, now) - SPEC.md is two documents

245,112 lines and 5,212 numbered headings; by top-level section, **137,687
lines (56%) are package design notes** and 107,389 are kernel and system. 239
headings sit out of numeric order among their siblings (e.g. 22.15 at line
52774 before 22.6 at 53330), because "the next free number" is appended
wherever the author is. The binding kernel contract is a minority of the
document that claims to be it.

Recommendation: move each package section into its own `<PKG>-SPEC.md`, on
the WEAVE-SPEC/ARCADE-SPEC/C64-SPEC precedent, KEEPING its number (`DOS-SPEC
§96.40.1`), so the move is a mechanical citation rewrite that `checkdocs`
already resolves (`<DOC> §N`). SPEC.md then holds the kernel, the ABI and the
cross-package standards (§47, §6.6, §66). A generated table of contents in
INDEX (number -> line) fixes navigation without renumbering. Prior art:
Inside Macintosh was a manager per chapter; Minix kept its kernel contract
small enough to print with the source. Bytes 0.

### engsys-19 (rethink, medium, now) - machine-readable routine contracts

Hard rule 3 (preserve everything but the documented outputs) has no gate: the
stack walker checks balance, not clobbers. The kernel already writes the
contract in a near-uniform header - 1,476 `; out:` lines and 804 `;
clobbers:` lines over 3,506 top-level labels - but nothing parses it, and the
headers drift like everything else (`wm_content` says "AX = content left
(W_X+1)", wm.inc:5697, and returns W_X for a flush window, wm.inc:5705-5707).

Recommendation: fix the grammar (`in:`/`out:`/`clobbers:`/`flags:`/`lock:`),
then (a) generate a kernel routine reference from it, AmigaOS-autodoc style,
and (b) add a conservative checker, report-only first: a public routine whose
straight-line body writes a register that is not in `out:`/`clobbers:` and is
not saved and restored is a finding. GEOS's `PASS:`/`RETURN:`/`DESTROYED:`
headers and MASM's `PROC USES` are the precedent. Bytes 0.

### engsys-20 (prior-art, low, now) - a checked build at the API boundary

Windows 3.1's debug KERNEL validated handles and parameters at every API entry
and logged to the debug terminal; NT shipped a checked build. Many SDK
contracts here are runtime-checkable preconditions: "caller holds the gfx
lock", "BX = a window of YOURS", "UI task only", DF = 0. Recommendation: a
`CHECKED=1` knob that wraps the RSLOT/SLOT dispatch with those checks for the
cells whose contract names them, reporting through the debug registry
(§57). Knob only: 0 shipped bytes, and guard 2 is the only bound on a knob
build. A soak row driving a few packages under it would find contract breaks
no review can.

### engsys-21 (weakness, low, now) - the Makefile is one 20,872-line file

53% of it is comment (11,044 lines), with one `include` (apps/cc/Makefile.inc,
line 10105). The comments are mostly incident records, which is valuable, but
each is a claim that rots in place (Makefile:22, Makefile:2547 above).
Recommendation: split by subject into `mk/*.mk` on the apps/cc precedent, and
move long incident narratives to the doc that owns the subject, leaving the
rule and a pointer; `#>` markers and `os88help.py` are unaffected. Bytes 0.

## Rethinks

- **One source per fact, the rest generated** (engsys-3, -5, -6, -10, -18,
  -19). The tree already proves the model three times: `os88index.py`, the
  kernsize baseline block, the `ks:` facts line. Extend it to §3's constants,
  §15.1's guards, §20.3's ledger and contract block, the tier tables in
  TESTING.md/WRITING-TESTS.md, AGENTS.md, and a routine reference from the
  proc headers. What stays hand-written is the WHY, which does not go stale
  when a number moves.
- **Names over numbers for cross-references** (engsys-2, -17). A heading-drift
  check is the cheap version; stable anchors are the full one.
- **Admission by declaration** (engsys-7): a row says which family it is in,
  and the gate holds the family rules.
- **A checked build** (engsys-20): turn the SDK's preconditions into
  assertions in one knob.

## Prior art

- **MS-DOS** (Programmer's Reference): one entry per function in a fixed
  Entry/Return/Notes shape, version-gated ("2.0+"), one error table. os8088's
  SDK comment per `%define` is that shape; what is missing is the per-slot
  "since" - `abi.lock.json` has the history but no slot-level index.
- **Windows 3.0/3.1**: `windows.h` and the `.DEF` export ordinals were the
  contract; the debug KERNEL validated parameters (engsys-20). Ordinals were
  never reused; os8088 deliberately renumbers in alpha (§20.8 rule 4) and
  compensates with `t_api_abi` and the lock, which is better for a tree that
  rebuilds every caller.
- **OS/2 1.x**: one API definition with generated language bindings (H2INC);
  Family API marked which calls existed in real mode. The analogue here would
  be an INDEX column saying which cells refuse on kern_small or kern_mega -
  `t_api_abi` already knows.
- **Mac System 5-7** (Inside Macintosh): a chapter per manager with a closing
  Summary listing every constant, record and routine with its register
  interface; later volumes marked what they superseded instead of rewriting
  the old chapter. os8088's `apps/os88api.inc` is the Summary; SPEC's §1-§4
  and §15-§17 are the old chapter that never got its banner (engsys-4).
- **GEOS**: the Esp assembler checked conventions, and every routine header
  read `PASS:`/`RETURN:`/`DESTROYED:` - the clobber list as data (engsys-19).
- **AmigaOS**: autodocs, the reference manual extracted from source headers.
- **Minix**: a contract small enough to print beside the code; the lesson for
  engsys-18 is size, not format.
- **NT design workbook**: specs before code, checked builds, build-lab
  verification tests - the shape `fast`/`full` already has.

What os8088 does better than all of them: assembly-time content assertions,
an ABI lock with written reasons, the suite registry with `why=`, and a
generated "does this exist?" index. None of the era's projects had a gate on
their own documentation at all.

## Doc/code mismatches

Each with its file:line, a short exact quote, what the code does, and the
replacement. These are text fixes; none changes a byte under `build/` except
the os88api.inc comment, which moves the PKG_FMT fingerprint (no bump).

1. SPEC.md:12 - "**Sections 46 to 49 are unused.** §45 is the Tracker; the
   four above it are" - §46-§49 are ArtfulType, the greying standard, Missile
   Command and TameGram; the one unused number is 104 (103 is REDLINE, 105
   MIDIRack). -> "**Section 104 is unused.** REDLINE is 103 and MIDIRack 105;
   the number is free for whatever comes next." Also drop "45"/"49" from
   `RULE_REFS` and its comment (checkdocs.py:93-95).
2. SPEC.md:102 - "CS = DS = `KERNEL_SEG` (0x0800)" - KERNEL_SEG is 0x0060
   (kernel.asm:27); LOW_SEG is `FAT_SEG + FAT_PARA` (kernel.asm:1798). ->
   "CS = DS = `KERNEL_SEG` (0x0060) ... **SS = `LOW_SEG`** (computed, the rung
   above the FAT snapshot, §2.1)".
3. SPEC.md:106 - "modules in" / "there is no far code and no second code
   segment (§33)." - `.cold` runs at COLD_SEG and is called far (COLDCALL,
   kernel.asm:5524-5530; 363 far-call sites). -> "Calls between modules in
   `.text` are near. `.cold` (§2.6) is a second code segment, entered far
   through `call COLD_SEG:` and left by `retf`; `.vga` (§2.10.9) is near on
   kern_big."
4. SPEC.md:131 - "Two further sections exist" / "All four are declared once" -
   about thirty are declared at kernel.asm:2142-2269. -> "The sections are
   declared once, with their attributes, at the top of `kernel.asm`
   (`.boot2`, `.text`, `.bss`, `.lowbss`, `.vgabuf`, `.cold`, `.vga`, `.cfx`,
   `.mega`, `.ovl` and one per module image)."
5. SPEC.md:128 - "kernel.asm's own final `section .bss` block (§15) is the last
   thing" - the last thing is the section-sentinel block (kernel.asm:9786-9886),
   which must stay last. -> "kernel.asm's end-of-file sentinel block (§15.1) is
   the last thing assembled, and must stay so."
6. SPEC.md:161 - "greyed the way §46 says" -> "greyed the way §47 says".
7. SPEC.md:26453 - "(§39.4/§46)" -> "(§39.4/§47)".
8. SPEC.md:26463 - "Paint does both (§46.3)" -> "Paint does both (§47.3)".
9. SPEC.md:26555 - "It is the §46 rule 5 case" -> "It is the §47 rule 5 case".
10. SPEC.md:55757 - "a flag and not a colour** (§46)" -> "(§47)".
11. SPEC.md:76640 - "the disabled flag (§46 rule 1)" -> "(§47 rule 1)".
12. SPEC.md:4041 - "KERNEL_SEG   equ 0x0800" - also `KERN_BUDGET equ 74240`,
   `STK0_SIZE equ 1024`, `VIEW_KB equ 3` in the same block. Code: 0x0060;
   derived 100,864/64,000/68,096; 512; 1 (kern_big/mega) or 2 (kern_small).
   -> replace the block with a generated one, or correct the four lines and
   write `KERN_BUDGET equ KERN_RESIDENT_KB*1024 - KERNEL_SEG*16 ; per arm`.
13. SPEC.md:4092 - "**boot splash entry at 0x0008** (§15)" - 0x08-0x0B are
   free (kernel.asm:2359-2365). -> "the fixed low cells (0x0000 `jmp kmain`,
   0x0004 `boot_cylrun`, 0x000C `boot_ticks`, 0x000E the debug registry, §57)".
14. SPEC.md:4132 - "The table is the whole of `kernel/`, and that is now
   literally true." - 35 of 74 files are absent. -> add the 35 rows, then
   keep the sentence and gate it (engsys-4).
15. SPEC.md:32330 - "%if KERN_SIZE > KERN_BUDGET" (and :32358 "against 86,528
   bytes") - guard 1 is `KERN_SIZE > KERN_BUDGET + KERN_GRACE` with a kern_mega
   arm and a `KERN_KNOB` skip (kernel.asm:9469-9477); guard 5 includes
   `BOOT2_PAD` (kernel.asm:9682). -> replace the fenced block with a pointer to
   kernel.asm's guard block, and "against `KERN_BUDGET`, derived per kernel
   (100,864 on kern_big, 64,000 on kern_small, 68,096 on kern_mega against
   `MEGA_RESIDENT`)".
16. SPEC.md:34169 - "`make bench` builds both onto `build/bench.img`" - no
   such target in the Makefile; `tests/fontbench`, `tests/typebench` exist. ->
   state the targets that do build them, or "they have no `make` target today".
17. SPEC.md:34214 - "menu bar (logo + File" - the bar is the System menu and
   Locator (kernel/menu.inc:10-16); §17 also says "two Timers" (a package,
   §111) and SPEC.md:34272 "segment 0x0800" (0x0060). -> rewrite §17 against
   `bootsmoke`'s and `kernresident`'s assertions, or move it to docs/history.
18. SPEC.md:44322 - "`kernel.asm` asserts the table's start; its span is
   whatever" - kernel.asm:4494 also asserts the exact length from the cell
   counts. -> either drop that guard (engsys-6) or say "and its length, from
   the cell counts".
19. SPEC.md:44378 - "and `osapi_table_end` is `0x04F5`." - OSAPI_VOL_CHECK is
   at 0x04F5 (os88api.inc:2567), a 119th rare cell; the table is 1,259 bytes and
   ends at 0x04FB. -> "`OSAPI_VOL_CHECK` (§118.14) at `0x04F5` a 119th, **1,259
   bytes**, and `osapi_table_end` is `0x04FB`" - or delete the ledger; it ends
   "Those counts are the tree it landed on, not a contract".
20. SPEC.md:44411 - "The fifteen, each with the access" - the table has 19 rows,
   the guard 17 ICELLs plus kern_mega's arms. -> "The inline cells, each with
   the access".
21. SPEC.md:44813 - "BLOCKS for the clip" - the door is GRANT/RELEASE
   (os88api.inc:539-545). Listed: ABI-BREAK-LIST §11. -> the SDK's text.
22. SPEC.md:70989 - "Cold code is ordinary code, near" - `.cold` is a far
   segment (§2.6). -> "Cold code goes in `.cold` (§2.6), which relieves the 64KB
   window and is still RESIDENT: if the footprint has to shrink it shrinks by
   doing less, or by becoming an on-demand module (§2.8)."
23. SPEC.md:113326 - "`DSV_SIZE` is 26" (and the fenced `DSV_SIZE equ 28`) -
   36 in kernel/driver.inc:557 and drivers/os88drv.inc:410. -> "`DSV_SIZE` was
   26 when this was written (it is 36 today)", and the fence marked as history.
24. apps/os88api.inc:892 - "AL = 1 enter (BX=win ptr) / 0 exit;" - BX binds on
   exit too, and CF=1 refuses either verb when another window holds the screen
   (wm.inc:4287-4313). -> "AL = 1 enter / 0 exit, BX = YOUR window either way;
   caller holds the gfx lock; out CF=1 refused - the screen is another
   window's, entering or leaving". `abilock.py --update`, no bump.
25. kernel/kernel.asm:2783 - "everything ADDED since starts at" (0x0200) - the
   opposite of §20.3.1. Listed: ABI-BREAK-LIST §11. -> delete the block
   comment; §20.3.1 is the rule.
26. kernel/kernel.asm:9787 - "TWELVE LABELS AND NOT TWO" - 20 sections are
   checked. -> "TWENTY LABELS AND NOT TWO".
27. kernel/wm.inc:5697 - "AX = content left (W_X+1)" - W_X when the window is
   flush with the screen's left edge (wm.inc:5705-5707). -> "AX = content left
   (W_X+1, or W_X for a window flush left, SPEC.md 11.95.2)".
28. docs/TESTING.md:198 - "37 rows, ~13s" (and :241 "43 rows", :242 "10
   rows.", :243 "The other 907 gates") - 76 fast rows, 14 full, 1,944 soak.
   -> drop the counts and point at the banner, as :245 already says.
29. docs/WRITING-TESTS.md:138 - "The five rows:" (and "Five more") - full has
   14: add `w16censall`, `knobself`, `khpknobs`, `noovl`, or generate the list.
30. Makefile:2547 - "host-side regression tier (~2s)" -> "host-side regression
   tier (budget 30s CPU)".
31. Makefile:22 - "`all` is 1,800 lines down" -> "`all` is far down the file".
32. CLAUDE.md:632 (and AGENTS.md:534) - "it is 6,553 lines" - 15,235 lines. ->
   "it is over 15,000 lines".
33. CLAUDE.md:336 (and AGENTS.md:305) - "there is no far code (§33)" - `.cold`
   is reached far. -> "Every inter-module call within `.text` is near; `.cold`
   is reached far (§2.6)."
34. CLAUDE.md:953 - "every file in `kernel/` is in" - 35 are not (item 14). ->
   true again once §4 is completed and gated.
35. CLAUDE.md:5 (and AGENTS.md:5) - "carry the same repository guidance" -
   726 differing lines. -> regenerate AGENTS.md (engsys-10).
36. docs/README.md:74 - "two documents may not share a" basename - 26 do. ->
   fix the resolver (engsys-8) and replace the sentence with how an ambiguous
   qualifier is resolved.
37. tools/os88index.py:5 - "SPEC.md is ~67,000 lines" -> "~245,000 lines".
38. apps/paccman/pmc_menu.c:121 - cites SPEC.md section 91.14; §91 has no
   subsections. -> "(SPEC.md 91)".

## Open questions

- The soak's wall time: CLAUDE.md:697 and :1255 and TESTING.md:279 say "nearly
  two hours"; the registry DECLARES 33.7 serial hours for the 1,595 native
  rows and 8.2 for the ports. Either the declarations are high or the sentence
  is old; the next full soak should say which.
- Do the owner's tier exceptions cover the WIN16 rows in `fast` and
  `w16censall`/`knobself`/`khpknobs`/`noovl` in `full`? If yes, engsys-7 is a
  documentation fix; if no, ~14 declared seconds leave `fast`.
- Should `docs/plans/completed/` be warn-only under engsys-1? It is design
  record, not measurement, and stale citations there do mislead.
