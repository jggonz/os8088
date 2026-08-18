# What the CWORD port learned

`apps/cword` is Microsoft Word 1.1a, ported to os8088 as a C package across
four working sessions: the one that built the C toolchain and a demonstrator,
the one that built the overlay mechanism and did the port proper, the one that
added Page view and typefaces off the system disk, and the one that polished
the product name, the About box and the disk images. This file is what those
sessions hit, **in the order the next port will hit it**, each with what it
cost and what fixed it. Every agent the `port-to-os8088` skill spawns reads it
first, and the point of reading it is not to be interesting — it is that
every item below assembled cleanly, booted, and was wrong, and most of them
were found by counting or by a screendump on the wrong adapter rather than by
a crash.

Where a section of `SPEC.md` owns the reasoning it is cited; `docs/C-TOOLCHAIN.md`
has the mechanics of the compiler and the gate and is not repeated here.

---

## 1. What "port" meant, and what it did not

- **The user interface is the original's, taken from its source and not
  from memory.** CWORD's nine menus, every string and mnemonic, the shortcut
  captions, the ribbon's six buttons, the ruler's controls, the status line's
  field order and the dialogs' contents each came from ONE named file in the
  reference tree (`Opus/resource/menus.cmd`, `keys.cmd`, `ibdefs.h`,
  `status.h`, `dlg/*.des`) — and that table of surface → file is the first
  thing in SPEC.md §70.12. Build that table before writing a line. When you
  find yourself typing a menu string you did not just read, stop.
- **The code did not port and was never expected to.** Word was pcode built
  by a proprietary compiler against the Windows 2.x API. What carried was
  *tables, formats and behaviour*: the RTF keyword table was transcribed whole
  (~70 keywords, everything else through an ignore path); bit-field records
  became byte-offset records with build-time self-checks; every 32→16-bit
  narrowing was written down. Expect the same of any reference: survey what
  would compile in this C (K&R, `long`, bit-fields, anonymous unions, heap
  handles → almost nothing) and plan a **reimplementation of behaviour**, not
  a compile.
- **What cannot be carried is present and greyed with the FACT that greys
  it** (SPEC.md §47) — "no printing path in this OS", "no Undo in this build"
  — never silently missing and never faked. CWORD greys the Print family,
  Spelling, Thesaurus, the Macro menu, Outline, footnotes, fields, tables,
  Paste Link, Summary Info, Glossary and Undo, and each greying names why.
  View > Page was greyed as a fact until the session that built it.
- **A shipped sample document may not claim what the reader cannot render.**
  The Word disk's `WELCOME.DOC` demonstrates centring and double spacing;
  cword's RTF reader carries character formatting only, so a verbatim copy
  would have rendered *"This paragraph is centered"* flush left. `WELCOME.RTF`
  says those formats can be set *in the program* (true) and names the three
  narrowed spans in prose instead of wearing them (§70.12.3).
- **The licence question is the user's.** The reference had no LICENSE and
  every file said "Copyright (C) Microsoft". Four options were put to the
  user (clean-room; lift tables, keep unreleased; lift and ship; pause) with a
  recommendation; the user chose to lift the tables and ship. The attribution
  went into every file header carrying derived material, into the About box,
  and into the PR body. Ask this once, early, and record the answer.
- **Two programs may share an ambition; they may not share a name.**
  `apps/word` (assembly, `WORD`, `.DOC`, `vm/xt-word`) and `apps/cword` (C,
  `CWORD`, `.RTF`, `vm/386-c-word`) share no file, target, image, vm dir or
  extension, by rule, "because the next person to touch either will silently
  get the other" (§70.12). Check every name against `apps/`, `vm/`, the
  Makefile and `build/` before wave 1.

## 2. Measure before scoping

- **Build the ceiling before you plan against it.** The port opened by
  building both existing packages and quoting `os88pkg`'s line: `apps/word`
  at 54,952 of 61,440; the C demonstrator at 47,136 for a fraction of the
  function. That one measurement produced the conclusion — C costs 2–3× the
  image of hand assembly, so the port does not fit in one segment — and the
  user's decision that followed. Never estimate what one `make` will tell you.
- **When memory is the constraint, measure per function.** For the typeface
  work (2,350 bytes spare, a 2,850-byte library to add) a nasm `-l` listing
  of the shim plus a small Python pass attributing bytes to each `_cw_*` label
  turned every move into a decision instead of a guess. It cost one build.
- **Reproduce a documented blocker before designing around it.** SPEC.md's
  note that the assembly overlay froze on UI callbacks was stale — three
  variants and seven round trips disproved it in minutes, and the note was
  fixed. A plan that routes around a defect nobody has re-checked is paying
  for a ghost.
- **A capability gate that puts numbers on the glass beats reasoning.**
  `tests/covl` prints `Loaded: yes`, a counter only the module can bump,
  `ovl_mix(1,2,3,4) = 1234` (a wrong argument fixup reads the saved CS, "a
  large and obviously wrong number rather than a plausible one"), and a
  callback that reports what it was told — which found the shim bug static
  review missed ("told 10 saw 49"). Press the key ten times: a two-byte stack
  leak works for a while. Build the gate before the feature that needs the
  mechanism.
- **Prove a lowering, don't argue it.** The instruction lowerings in
  `cc8086.py` were checked by a boot floppy running the 386 form and the 8086
  replacement from identical state and diffing registers and FLAGS — 428
  cases, with a **negative control** injecting broken lowerings so a clean run
  means something. Any new tool in the chain gets the same treatment.

## 3. The compiler, in practice (what `C-TOOLCHAIN.md` does not say)

- `smlrcc` needs `smlrc` on `PATH` (`sh: smlrc: command not found`);
  `apps/cc/Makefile.inc` sets it. Never pass `-nopp`.
- **`Identifier table exhausted` is a build flag, not a patch.** SmallerC's
  tables are `#ifndef`-guarded constants; `tools/setup-cc.sh` builds it with
  `CFLAGS="... -DMAX_IDENT_TABLE_LEN=32768 -DMAX_MACRO_TABLE_LEN=16384
  -DSYNTAX_STACK_MAX=16384 -DMAX_CASES=512"` — via `CFLAGS`, not `CPPFLAGS`
  (which carries `-DPATH_PREFIX`/`-DHOST_MACOS` and would be replaced, giving a
  compiler that cannot find its includes). Changing them does not retrigger
  the freshness check: `make clean-cc && tools/setup-cc.sh`.
- SmallerC will not parse `-32768` (`Constant too big`): write `-32767 - 1`.
- **`char` is signed and `int` is 16 bits.** `char c; if (c == 200)` is never
  true; use `unsigned char` for a byte.
- **One translation unit is one namespace.** `Invalid or unsupported
  redeclaration of 'cw_pap'` — the RTF reader already owned the name in an
  `#include`d file. Grep every included file for a name before declaring it,
  and rename with a word-boundary regex (`\bcw_pap\b(?!_)`), because a plain
  sed also hits `cw_pap_*`.
- **`imul` refusals are shape problems.** `imul ax, ax, 54 … needs a scratch
  register … none is provably dead` → `(at<<5)+(at<<4)+(at<<2)+(at<<1)`;
  `imul ax, ax, 8 … FLAGS is live` → split the expression so the multiply is
  not the last thing before a test. Make strides powers of two
  (`CW_SH_STRIDE 128`) so `row*stride` is a shift and never an `imul`.
- **`binary output format does not support external references` names a
  symbol you called and never defined** — it surfaces at nasm, not at the
  compiler, and for an `ovl_*` shim table it surfaces from a line in
  `build/*.gen.asm` you did not write. The name in the message is the answer.
- The shim's `%include` path is relative to the nasm `-I apps/ -I build/`
  line: `%include "cword/cwmove.inc"`, not `"cwmove.inc"`.
- `%error` inside an `%include`d file was silently dropped on nasm 3.02;
  guards in the runtime use `%fatal`.
- **The host harness compiles the same C with clang**, which is stricter about
  prototypes: `call to undeclared function`, `static declaration follows
  non-static declaration`. Put prototypes at the top of the main `.c`.

## 4. The four rules, as they were actually met

- **Every out-parameter is a static.** `struct os88_pt org; os88_wm_content(w,
  &org)` is refused; `static struct os88_pt org;` is the idiom. Half the API
  is out-parameters. A `static` inside a function is fine and is not
  re-entrant — a worker and a callback sharing it will pre-empt each other.
- **A struct table may be INDEXED; only copying is forbidden.** The first
  menu table was five parallel arrays "so nothing could be copied by
  accident" and was wrong on day one — four items dropped, every attribute
  below out of step. `static const struct cw_item cw_it[]` indexed by row
  copies nothing. Parallel arrays are the error-prone option.
- **A refused `rep movsb` under a bare `L<n>:` label with no function above
  it means a struct went by value somewhere** — an argument, an assignment or
  a `return`. Grep for `struct` on the right of `=` and in argument lists.
- **Frames were never the problem** — 26 bytes maximum in 158 functions
  against the 96 cap — because every buffer is static. Keep it that way; the
  worker task has 256 bytes of stack in total.
- **The one hand-written byte mover is the only place ES is loaded**, and it
  is tested on a real x86 with `SS != DS` by a boot-sector harness
  (`hosttest/cwmovetest.asm`) with negative controls, including "ES not
  restored → the machine never returns". `os88_memcpy` is ascending-only and
  smears an insertion; if you need overlap-safe moves you need the same
  routine and the same test.
- **A new assembly shim is a new host stub, in the same edit.** Three times
  the harness failed to link (`Undefined symbols … "_cw_ty_fit"`) because a
  shim went into `cwtype.inc` and no stub into `hosttest/cwuitest.c`. The build
  tells you three steps late.
- **A shim that answers in AL must `xor ah, ah`**, and a shim that passes text
  to a kernel routine that reads `ES:` must load ES = DS and put it back —
  ES is `KERNEL_SEG` on every callback and stays so.
- **`OSAPI_GFX_PEN` is a setter whose argument is CF, and the cell preserves
  every flag.** A routine that read the carry back as if it were a query
  greyed a whole band whenever the caller happened to arrive with CF set — a
  bold+underlined run drawn as a 50% checkerboard, seen once. `clc` before
  the call. Read a slot's contract for FLAGS as well as registers.

## 5. The budget, and the overlay

The numbers, so the next port can do the arithmetic before it starts:

| | resident image | bss | overlay | spare of 61,440 |
|---|---|---|---|---|
| the C demonstrator (3 kernel menus, 4,000-char doc, RTF tables) | 20,086 | 27,050 | — | 14,304 |
| after the port proper (nine menus, dialogs, search, utilities) | 37,062 | 22,028 | 9,430 | 2,350 |
| after Page view (+772) and the check mark | 37,886 | 22,032 | 9,430 | 1,522 |
| RTF engine moved out | 32,874 | 22,032 | 14,646 | 6,534 |
| + type library + proportional row model | 38,938 | 24,507 | 14,646 | **−2,005 (refused)** |
| clipboard + paragraph commands moved out | 36,752 | 24,507 | 17,097 | 181 |
| Font panel moved out | 35,808 | 24,507 | 18,310 | 1,125 |
| shipping | 35,886 | 24,511 | 18,564 | 1,043 |

- **~9,800 lines of C = ~36KB resident + 18.5KB overlay + 24.5KB bss.** Use
  that ratio for the estimate in the plan, and expect the first full build to
  overshoot: the first one here was `image 36236 + bss 27718 = 63954 exceeds
  budget`, fixed by halving a staging buffer (`CW_RTF_MAX 12000 → 6000`).
- **bss is the cheap half** — it costs no file bytes and the loader zeroes
  it. An *initialised* array in `.data` is paid for twice (floppy and
  region); a 6-byte three-field menu-set struct was chosen over the 36-byte
  library one for exactly that reason. The next byte to save in CWORD is a
  buffer moving from bss to a heap claim, not code.
- **Track the size line from the first commit** and treat 55,000 resident as
  the trigger: at that point the next wave's first job is the split. CWORD hit
  the ceiling mid-feature twice, and both times the fix was moving code out,
  which is cheap only if the code is already the kind that can move.
- **Split by FREQUENCY, never by size** (§70.14). What a keystroke touches —
  layout, wrap, the shadow, the chrome, the keyboard, the type library — stays
  resident; what runs once per command — dialogs, file in/out, search, Sort,
  Renumber, TOC, the clipboard, the paragraph dictionary, the Font list —
  goes out. Done that way the resident image got *smaller* while the program
  grew a view and a text engine.
- **Renaming a function to `ovl_*` is the entire mechanism.** A regex rename
  across every `.c` file *and the host harness* is the operation; the counters
  `cc8086.py` prints (`N functions moved to .modc`, `M resident shims`, `K
  loading call sites`) are how you know it took.
- **Only code moves.** Every global, literal and bss byte the moved code
  names stays resident and DS-relative; that is what makes it possible in C
  at all. Nothing in an `#include`d shared assembly file (`apps/os88type.inc`,
  which `apps/word` and `tests/facetest` also include) may be `%ifdef`'d into
  a section — leave shared code resident and move the UI around it. The
  useful side effect: the Font list works on a disk with no `.OVL`.
- **An overlay function answers a status and 0 means it did not happen.** A
  refused load (no heap, no file, stale module, a worker task asking) toasts
  the reason and returns 0 without running (§47). Design the callers so 0 is
  a normal path.
- **Never call an `ovl_*` from a worker task, and never take one's
  address.** The worker gate is decided from SP, not from a flag a task that
  never exits raised once ("works until the app spawns a worker and never
  again"). `cc8086.py` refuses the address by name.
- **The `.OVL` is resolved in the launching instance's current directory**
  (§19.2.1), and a double-click on a document leaves that directory on the
  *document's* (§54.9). So package, overlay and welcome document are **three
  files in one folder** on every disk they share — which is why each Word has
  a folder of its own on `apps-all.img` and never a place in `APPS/`
  (§19.9). A disk with the package and no `.OVL` is a program whose every menu
  refuses, politely.
- **Two calling conventions meet at the overlay boundary and the shim is
  where copying an assembly idiom into C breaks.** A `call`/`retf` shim is
  right when arguments travel in registers and fatal when they are on the
  stack (a far call pushes four bytes of return, so every `[bp+N]` for N ≥ 4
  is off by two). It is solved once, in `crt0.asm` and `cc8086.py`; do not
  write your own.

## 6. The redraw path is the design, not the polish

PERFORMANCE.md's prices — 756 µs a `gfx_*` call, ~900 µs a glyph cell, 46.7 µs
an API call — do not change because the source is C, and C makes the wasteful
structure the natural one to write. What CWORD does, and what it costs:

- **The glass is shadowed.** `cw_sh[]/cw_sha[]` hold the character and
  attribute of every cell on the screen; a repaint lays out the rows that
  *could* have changed, compares each against the shadow, and draws only the
  columns that differ. A keystroke is **5 calls and 8 cells**; the one that
  scrolls is 8 and 9; a full repaint of the 69×24 view is 132 calls and 1,899
  cells — **1.8 seconds** on the target, which is what a screen of text costs
  whoever draws it. Design this in wave 1. A full repaint per keystroke is a
  defect, not a first draft.
- **The layout stops early.** After an insertion the rows below hold the same
  text one byte along; the moment a row's new start equals its old start plus
  the delta, `cw_relayout` stops.
- **A row's break is decided by the character one PAST its end**, so an edit
  in row N can move row N−1's break: relayout starts one row *above* the edit
  (`cw_from--`). Found by the harness; the glass had kept "...three times" for
  "...three times acr" through a whole session.
- **The row pitch is 11, not 10, and it is a variable.** The italic
  overstrike sits one pixel above the glyph band and the double underline two
  below; at pitch 10 the rule landed exactly where the next row's italic goes
  and rubbed it out on every repaint. Model every pixel row outside the band
  separately, and let a face off `FONTS/` bring its own height.
- **One decision per cell, one call per RUN.** Bold is a second strike one
  pixel right, italic a strike up-and-right, the underlines are drawn rules,
  small caps a case map — each ONE extra call per run, never one per glyph.
  A row of proportional type composes into a 1bpp band in the package's RAM
  and goes down with **one `gfx_blit1`**; the three per-character walks
  (`ty_fit`, `ty_pen`, `ty_hit`) are assembly called once a row.
- **The ruler scale was ~70 `gfx_pixel`/`vline` calls** — "54 ms and a third
  of everything a full repaint did" — until it became one `gfx_blit1` band
  (203 → 132 calls). Anything drawn from a loop of primitives is a band
  waiting to happen.
- **The status line delta-draws** (was 44 cells a keystroke to change `Col
  n`); **a scroll's vacated row is one white fill plus its text** (79 → 9
  cells), and that test must be first in the shadow-compare chain.
- **Paste opened the hole once.** One `memmove` per pasted character on a
  4,000-char document is 16MB moved — about four minutes on the target.
- **After any panel owns the glass, invalidate the shadow.** A dialog, a
  menu, the About box, the Standard File dialog: `ovl_dlg_paint` clears
  `cw_sh_ok` and the caret flag; `os88_onfile` always redraws, because after a
  Save the row read `iH#there bold` — the shadow still described what the
  dialog had covered. The harness's shadow-vs-glass audit is what names the
  step at which the shadow starts lying.
- **`os88_wm_destroy` without the gfx lock does not take** — the window
  hides, the dock tile stays, the program is unreachable and resident. Bracket
  it with `os88_gfx_lock/unlock`, then `os88_task_alive(win)` *outside* the
  lock. (There is no self-close slot; File > Close is a worker that sleeps 4
  ticks and does that.)
- **No C between `gfx_lock` and `gfx_unlock` that is not bounded by a count
  you can state.**
- **Cache on the pick, not on the row.** The pre-shifted glyph table costs
  ~160 ms once when a face is chosen (a third of one `int 13h`) for 2× off
  every line after; its one refusal is the carry deliberately dropped,
  because there is nothing the caller could do about it.
- **The three emulator-invisible defects** — a visible redraw, a double-draw
  flash, input overrun — never show in a screendump. The harness's cost table
  is how you see the first two.

## 7. The host harness: where the real bugs were

`apps/cword/build.sh` runs four host checks before anything is built for the
8086, and every one stops the build: the RTF tables check themselves; the RTF
reader and writer round-trip documents; **`hosttest/cwuitest.c` includes the
whole program against a stub `os88.h`, drives it like a user, rebuilds what the
screen ought to show from an independently written wrap after every keystroke,
audits the shadow against the modelled glass, and prints the cost table**;
`cwmovetest` runs the byte mover on a real x86. Three real defects came out of
the UI harness that no screendump would have shown (the row-above break, the
pitch collision, the modal panel and the shadow). Build the skeleton of it in
wave 1 and grow it with the program.

Traps in the harness itself:

- **A stub that always refuses measures the fallback path.** The stub
  `os88_gfx_blit1` refused, so the cost table priced the 71-call fallback
  until the stub modelled the real thing. Verify the stubs model what the
  machine does.
- **A cost fixture filled to the cap measures the refusal path** (0 calls per
  "keystroke"). Use 30 lines, not `CW_DOC_MAX`.
- **The scripts change with the product.** File > New started asking Word's
  save-changes prompt (`Tab, Enter`), About became modal (Esc, not a click),
  Copy needed a selection first, and a step left the caret at 0 so a
  selection copied nothing. Print the whole row — glass, model, shadow,
  document — on any failure.
- The stub `os88.h` grew with every API the program touched
  (`struct os88_mouse`, `os88_onmouseup`, `os88_worker`, `os88_font_char`,
  `os88_gfx_line/vline/pixel`, `os88_task_yield/sleep`, `os88_gfx_lock/unlock`
  …). Budget for that.

## 8. Fidelity: what the original does that this platform bends

- **More menus than `MENU_APPMAX` (5) means the bar is drawn in the window**
  (§12.2) — CWORD's nine titles are its own, with both period gestures
  (press-drag-release and click-open), Esc/arrows/Enter/mnemonics and
  Alt+mnemonic.
- **The kernel bar shows the instance name unless a menu set says
  otherwise.** The header's 15-char name is also the `.OVL` stem and the
  association, so it could not just say "Microsoft Word". The fix is the one
  `apps/word` uses: register an EMPTY menu set (count 0) whose `AM_NAME` is
  the product; its command handler is a stub that can never be called, but
  `CC_HAS_MENUS` declares it and the shim and the C may not drift.
- **The About box carries the product, the version, what this port is and
  the attribution — and nothing about how the build renders.** Draft-only
  notes were removed from it; what is synthesised or unimplemented is a fact
  about the build and belongs in the SPEC and in the greyed items.
- **The BIOS folds Ctrl-H/I/M into BkSp/Tab/Enter, so italic, hidden text
  and one indent have no Ctrl key here; Ctrl+Space arrives as ASCII 32 with
  the space bar's scan code**, and the "reset character formatting" binding
  ate every space the user typed ("The quick" arrived as "Thequick", seen in
  a zoomed crop). Route those four through the ribbon and a dialog.
- **The kernel face is characters 32..126.** `os88_font_char(0xFB)` for the
  IBM check mark drew NOTHING, and no checkable item had ever shown a check
  — it hid because every earlier checkable had its state visible somewhere
  else. Draft/Page was the first pair where the check *is* the answer. The
  check is two `gfx_line` strokes; the pilcrow is three drawing calls and
  rides through the row buffer as a marker byte. Anything outside ASCII is
  drawn, not lettered.
- **A dialog is TWELVE rows, which is a 640×200 number** (§39). The panel
  clamps to the content box but a control's y is `6 + row*10` and nothing
  clamps that: a 19-row About box put its OK button on the DESKTOP on CGA and
  Hercules. `6 + 10*10 + 11 = 117` against ~122. Adding a row to a dialog
  table looks right on VGA and is wrong on two adapters of three; the row
  count is a compatibility constant and carries a comment saying so.
- **A list wraps into columns sized from the live window height**, not from
  its length — the Font list ran off the bottom of a 200-line screen.
- **Grey rounds to black on 1bpp**, so a disabled item is a checkerboard and
  a ring is dotted; look at a greyed item and a dialog on `VIDEO=cga` before
  calling a drawing change done.
- **Mouse-up is delivered wherever it happens.** Choosing a menu item whose
  row lay over the text band selected from the caret to the release point;
  `os88_onmouseup` acts only if a press in the text set the drag flag.
- **A modal dialog's controls that overlapped its labels** (OK/Cancel at
  column 26 over text) and a **ruler whose numbers landed inside its
  buttons** were both found by cropping and zooming a screendump; the strip
  grew, the buttons moved. Look at every strip at zoom before believing it.
- **Ambiguity in the ask is enumerated, not guessed.** "Images with all the
  apps on 1.44MB disks, but keep the two Words separate" had a reading that
  rebuilt the shipped apps disks and one that added a fifth image; saying so
  in a sentence got the precise answer.

## 9. Files, make, disks

- **One translation unit, split by concern into `#include`d `.c` files**
  (`cwmenu.c` tables, `cword.c` model + engine, `cwchrome.c`, `cwdrop.c`,
  `cwcmd.c`, `cwovl.c`, `cwrtfio.c`, `cwrtftbl.c/h`), and **make cannot see
  through `#include`**: `$(BUILD)/<name>.raw.asm: <every included file>` and
  `$(BUILD)/<name>.bin: <every %included .inc>` — or an edit leaves a stale
  `.o88` and reads exactly like a change that did nothing. `touch` the main
  `.c` or `rm build/<name>.o88` when in doubt.
- `$(eval $(call CC_PACKAGE,<name>,<dir>,<NAME>.OVL))` — the third argument
  turns the overlay cut on (`tools/os88ovl.py --trim`); without it the build
  fails at `os88pkg: image size field != actual file size` once any `ovl_*`
  exists.
- **Every image in three geometries, each `os88disk.py --verify`ed** in the
  recipe (`--verify` is a standalone invocation and takes no other
  arguments). The 360KB build is where a port most needs re-checking: with
  CWORD on it the XT apps disk would sit at 347/354 clusters.
- **Nothing in `all` may need the compiler.** The C targets are on demand
  (`cword`, `cworddisk`, `allapps`), reach the compiler only through the
  `cc-toolchain` guard, and `cc-note` prints one paragraph when `build/cc` is
  absent. `make clean` spares `build/cc`.
- **A generated sample document shares the parser with the existing one**
  (`tools/os88rtf.py` imports `parse_line` from `tools/os88doc.py`) — a second
  parser is a second dialect. It is deterministic so the shipped file
  rebuilds byte for byte.
- **Verify a saved file from the host by walking the FAT12 directory entry
  to its cluster**, not by grepping the image for its magic — the first grep
  found the package's own string literals.
- **The C SDK has no build-time association block**, so `assoc=0` in the
  header and `.RTF` is not double-clickable on a cold boot until the program
  has run once (`os88_assoc_set()` is runtime). Known, ~15 lines in
  `crt0.asm` (§54.6), left as an SDK change. Say so in the plan if the port
  has a document type.
- **The 86Box machine is a copy of one that has booted**, with the B: image
  and the uuid changed and *nothing else*: 86Box does not reject an unknown
  `cpu_family`, it substitutes a default speed and rewrites the config on
  exit. It also rewrites the uuid on every launch — `git checkout` the cfg
  before committing, and never commit the `nvr/`. `RESET=1|cmos|flash|both`
  clears CMOS on the way in. One machine, not two: an XT target before
  anyone has *measured* the port there is a claim, not a machine.

## 10. Testing: the traps, in the order they bit

- **A stale QEMU answers on `build/qmp.sock` with the OLD image.** Every
  screendump succeeds and shows the previous kernel. `tools/qmp.py
  build/qmp.sock quit; rm -f build/qmp.sock` before every boot. When more than
  one checkout is in use, identify a QEMU by `lsof -p <pid> | grep cwd`, not by
  name — an agent once killed two QEMUs belonging to another checkout. Use a
  private socket path when the tree is shared (and keep it short: a
  scratchpad path over ~104 bytes is refused by QEMU).
- **A double-click is one process.** Two `mouse.py click`s decode as two
  single clicks; the launch needs two presses inside the 9-tick window. Every
  session wrote the same tiny driver — `to X Y`, then two `mouse_button
  1/0` pairs at 80/100 ms — plus `key:`/`shot:`/`sleep:` verbs. `mouse.py`
  with no arguments tracebacks (`IndexError`); read its source for usage.
- **`--screen` must match the adapter.** On `VIDEO=cga` clicks scaled for
  640×480 land nowhere; `mouse.py --screen 640x200`. Hercules is
  `VIDEO=herc HERCSEG=0x7000` and `tools/hercshot.py build/qmp.sock 0x70000`
  — `screendump` shows a black image there (docs/HERCULES-TESTING.md).
- **`VIDEO=` restamps `build/kernel.bin`.** Building a CGA kernel while
  another agent boots the shared `build/` changes what *they* boot; use a
  copy of the tree or serialise.
- **Save into a scratch copy** (`cp build/cword.img /tmp/scratch.img`) so a
  test's writes do not dirty the build image.
- **Crop and zoom before concluding a click was lost** — the check mark, the
  page ticks, the ruler and the eaten space were all `--crop … --zoom 8`
  findings.
- **The Bash tool resets cwd between calls**; `cd apps/x && …` heredocs fail
  with `no such file or directory`. Use repo-root-relative paths.
- **Patch with `assert s.count(old) == 1`** before every replace; one such
  assertion stopped a bad edit.

## 11. Working with agents on this

- **SPEC before code, and reconcile after.** The SPEC section was written
  first every time — `checkdocs.py` runs in `make` and rejects a citation to
  a heading that does not exist — and it drifted every time (reversed file
  names, phantom constants, numbers three builds stale). A closing pass that
  re-measures every number in SPEC, the source header and the README against
  the last build is part of the work: 37,078 → 37,084 → 37,062 was re-edited
  three times in one session.
- **Check for tree movement before a long run.** A `git fetch` mid-flight
  found main four commits ahead including a 9,000-line SPEC change; the
  workflow was stopped, the branch fast-forwarded, the brief edited, and the
  run resumed from cache.
- **Give every agent exclusive file ownership and a "do not re-derive"
  block of established facts.** Several honest "not mine to fix" reports
  followed; schedule an owner for the cross-cutting one-liners (an
  unterminated comment that ate `OS88_FDLG_SAVE` was reported by three agents
  and fixed by the fourth).
- **The adversarial audit found what boot tests could not**: a bss byte the
  loader never zeroed (odd `.data` length, `.bss` aligned one past the image
  end) and an `imul` lowering that refused ordinary `return i*10` because the
  liveness scan stopped at a label. Neither showed on the glass. Review with
  a lens that is trying to break it.
- **Only one hand in the translation unit at a time.** Reviewers read; the
  implementer and the fixer write, in sequence.
- **Commit and push only when asked**, one commit per coherent step, with the
  numbers in the message. Leave `vm/*/86box.cfg` unstaged when only its uuid
  moved.

## 12. What was the user's to decide, and what was not

Asked, with a recommendation each time: the licence posture; overlay-first
versus a maximal one-segment subset (the user chose overlay-first — the
mechanism, then everything); whether to trim the RTF tables (kept whole);
what "images with all the apps" meant. Everything else — the file split, the
resident/overlay line, buffer sizes, the four-number layout, drawing the menu
bar in-window, the empty menu set, greying Undo and Page as facts, the pitch,
the 12 rows, not touching a shared include, not fixing a pre-existing defect
outside scope (F8 extend, the dialog-dismiss sliver — reported instead) — was
decided, done, and written down. That is the ratio to keep.

## 13. What the RunCPM port added

`apps/runcpm` is RunCPM 6.9 (SPEC.md §71), ported across six waves by the
skill this file belongs to; `docs/RUNCPM-PORT-PLAN.md` carries each wave's
measured paragraph. What it learned that CWORD had not, one line each:

- **The `.OVL` cannot be loaded from `os88_main`** — there is no instance
  yet to resolve a module for (CWORD's `cw_doc_clear` rule, §70.14);
  ordinary file reads CAN, and the CCP and `AUTOEXEC.TXT` are read there.
  So an `ovl_*` the program needs first is called from the FIRST callback
  (the first wake here) and its refusal is printed as a fact where the user
  is looking (`Unable to load RUNCPM.OVL.` / `CPU halted.`), not toasted
  into a window that is not up.
- **Announce a long walk under the lock, before it starts.** A first `DIR`
  of a 77-file folder is seconds on the target; a toast raised under the
  gfx lock takes §59.4's immediate path (`RunCPM: reading A:0`) and the
  user knows the machine is not dead.
- **Toasts under a fullscreen window go to the terminal too.** The bar the
  toast lands on is under a `WF_FULL` window; every refusal goes through one
  `rc_say` that toasts AND prints the line on the console while fullscreen.
- **Measure a band before believing a per-cell guess.** The row composer's
  first loop was 306 µs a cell (7× the model's ~40) — one bench harness
  (`tests/rcband`, PERFORMANCE.md Set 65) turned the guess into 173 µs a
  cell + 860 µs a call, and the flush's whole pacing was re-sized on it.
- **A file read into a claim lands at a 512-ALIGNED offset**, never at
  paragraph 10h: SPEC.md §2.1.1's rule, and int 13h error 09h on real
  hardware whenever the claim's 64KB page boundary fell inside the file —
  ~13% of ZEXDOC launches — that QEMU's BIOS never showed.
- **`wm_geom`'s content height is `W_H − TITLE_H − 1`**, not `W_H −
  TITLE_H`: a window authored `TITLE_H + 200` tall showed 24 rows and a
  sliver, and the model's 25th row was never on the glass.
- **Adaptive slicing must count only EXHAUSTED slices.** A slice that ends
  early (blocked in a console read) counted as "fast", ~30 keystrokes
  walked the budget to its cap, and the next busy slice was 1.6 s of UI
  task; the step answers whether the budget was spent and an early end
  leaves the estimate alone.
- **A hot counter in the package's bss is a TCG slow path under QEMU** when
  that page also holds translated code (the per-branch budget decrement was
  5× on the whole core, in the OS only); keep it in the emulated machine's
  own memory or in a register — the 8088 is happier too.
- **A shared name scratch aliases across a refill.** The directory fill used
  the caller's `rc_n11` and an `F_OPEN` that triggered a refill opened the
  LAST file walked — exactly the trap the `static` idiom (§70.5) invites;
  give a walker its own scratch and make the harness refill before an open.
