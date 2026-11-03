# TELNET-PLAN — turning §70 into a BBS terminal

The design record for SPEC.md §70.8–§70.12. **SPEC.md is the contract and this
is why it reads that way**: what was asked, what was judged and rejected, what
was deferred with the arithmetic attached, and what the tree said when it was
read rather than assumed. A bare `§N` here means SPEC.md; this document's own
sections are cited as `TELNET-PLAN §N`.

Written before the code, amended by what the code finds — CLAUDE.md's rule.
The rows marked **not measured** are not measured; nothing here is a number
somebody hoped for.

## 1. What was asked

Turn TELNET from a deliberately dumb printing terminal into an **ANSI-BBS
terminal**: 16-colour attributes on an 80x25 screen, a full-screen text mode
that IS the board's own screen, the Telnet options and the keys a board
expects, and **Zmodem receive** with the file landing on a disk the user picks.
Uploads out of scope.

Everything in §70.8–§70.12 is that, and the interesting part is not the feature
list. It is that §70 had written down, in three separate places, the reasons a
64x18 buffer was right — and every one of those reasons was about a *printing*
terminal. §70.5: *"a terminal that reflows on a resize is one whose host has
the wrong idea of how wide it is."* §70.6: *"growing it to 80 for good is a
separate question with a real cost… and it is not answered here."* §70.1:
*"`NAWS` would have to report a window size the user cannot change."* The three
are one argument, and the reply to all three is one sentence: **the buffer is
80x25 always, so nothing reflows, nothing is guessed, and NAWS reports a
fact.** The viewport bargain §70.5 already struck is what makes that possible
without giving anything up.

## 2. Extend, or port a terminal emulator?

**Extend.** The parser is about 400 lines of assembly and the reference
implementations are all much larger than the thing they would be replacing.

### 2.1 What was judged

| candidate | what it is | why not |
|---|---|---|
| **SyncTERM's `cterm`** | the reference ANSI-BBS emulator, C, ~5,000 lines | it is a *screen library's* client — it draws through `ciolib` and owns no framebuffer of its own. The half that matters here (the state machine) is entangled with the half that does not (the abstraction over curses, X11 and Win32). Its BBS-specific behaviours are the valuable part and they are documented well enough to *read*, which is what was done |
| **minicom's `vt100.c`** | ~1,400 lines of C, a VT102 emulator | closest in size, and wrong in kind: it is a VT emulator, so its erases fill with the RESET attribute, its `CSI 2J` does not home, and its wrap is immediate. Every one of those is a visible defect on a board (§70.9.3, §70.9.5), so the port would begin by removing the behaviours that make it correct for its own purpose |
| **PuTTY's `terminal.c`** | ~8,000 lines | a scrollback architecture with compressed lines, dynamic allocation and `long` arithmetic throughout. Nothing in it survives the four C rules, and its size alone is more than the whole package's budget |
| **Amiga `Term`'s emulator** | a well-regarded ANSI/VT implementation | 68000 assembly against an OS with a text device. Nothing transfers but the ideas, and the ideas are in the specification |
| **Haberman's `vtparse`** | ~300 lines, a table-driven DEC-compliant state machine | **its SHAPE is what was taken and its code was not.** It is a generated transition table plus a dispatcher; the table is the expensive part in bytes and encodes DEC's C1 handling, which is exactly wrong here — §70.8 pins 0x80–0xFF as CP437 glyphs. So §70.9 is vtparse's states and vtparse's discipline (single byte in, no lookahead, no buffer) hand-written against the ANSI-BBS rules |

### 2.2 What §73's four C rules would have done to any of them

A C port would be a `§73` package, and the four rules are not stylistic:

* **Never take the address of an automatic.** SS ≠ DS, so `&local` is a stack
  offset dereferenced through the package segment. Every emulator above passes
  a `struct term *` around and takes the address of locals constantly.
* **No `movs`/`stos`/`scas`/`cmps`** — ES is the kernel's — so **no struct
  assignment, no struct by value, no struct return.** `cterm` returns and
  passes small structs; PuTTY assigns `termchar` values by the thousand.
* **No `long`, `float`, `double`, bit-field or anonymous union.** A Zmodem
  offset is 32 bits and every one of these emulators keeps positions in
  `long`. The assembly keeps them in `DX:AX` pairs, which is what the file
  slots take anyway.
* **Frames under 96 bytes**, which a recursive escape-sequence dispatcher does
  not respect.

And the decisive one is not on that list: **the package already exists**, in
assembly, and its two renderers share `con_scr` with the parser. A C parser
would be a third language boundary inside one package, reached across
`§73.14`'s overlay contract, to replace a routine that fits on a screen.

### 2.3 What WAS taken from the references

The behaviours, cited where they appear in §70.9: `CSI 2J` homes the cursor
(ANSI.SYS), erases fill with the *current* attribute (ANSI.SYS), wrap is
pending rather than immediate (every ANSI-BBS terminal), `ESC [ K` is End and
`ESC [ V`/`ESC [ U` are the page keys (the DOOR-game convention), `ESC [ M`/
`ESC [ N` introduce a music string terminated by 0x0E (SyncTERM's), and the
ZRQINIT auto-start sequence (every DOS terminal since Telix).

## 3. The forks, and how each went

### 3.1 The colour is composed, not lettered

The three candidates, priced on the 4.77 MHz 8088 the project targets, for one
80-column row:

| approach | calls | estimate |
|---|---|---|
| a glyph call per cell | 80 | **~72 ms** (PERFORMANCE.md's ~900 us a cell) |
| **compose an 80-byte × 8-row band, one `OSAPI_GFX_BLIT1` per attribute run** | 1–2 typically | **~3 ms** |
| pack the row as 4bpp pixels and one `OSAPI_GFX_BLIT4` | 1 | **~115 ms** (§5.4.1.3's planar decoder at ~106.9 cycles a pixel, 5,120 pixels) |

The band wins by an order of magnitude in both directions, and the reason
`BLIT4` loses is worth keeping: **one call is not the unit.** `BLIT4` is priced
per pixel because it decodes arbitrary colour; `BLIT1` is priced per *byte*
because the VGA's Set/Reset does the colour, and a text row is a 1bpp shape in
two colours per run. A full 80x25 repaint is **50 calls** composed and **2,000**
lettered.

The estimates are arithmetic off PERFORMANCE.md's table, **not measured**.

### 3.2 The refused pen pair — the kernel changed, not the caller

§5.4.2.2 refused `(ink, paper)` unless one colour's plane set was a subset of
the other's, because one blit writes every pixel of its rect and two exact
non-nested colours cannot come out of one pass. Green on red was refused;
anything on black and white on anything were not. **That is most of the 128
pairs a board can send.**

**The decision is to lift the refusal in the KERNEL** — §5.4.2.2.1's Map Mask
split, which is the fix `vga12.inc`'s own comment at the refusal has named
since the pen landed (*"both: a Map Mask split, which this does not do"*). A
refused run costs two emits instead of one and every colour is exact.

The five ways out, and why four of them lost:

| way out | exact? | cost for a 40-cell run | verdict |
|---|---|---|---|
| **§5.4.2.2.1's Map Mask split, in the kernel** | **yes** | two emits, ~29.5 clocks a byte against 12.5 | **taken, and it landed** |
| `OSAPI_GFX_BLIT4` for the refused run | yes | 2,560 px × 106.9 cycles ≈ **57 ms** against ~0.8 ms | the fallback if the split did not fit the budget — **not needed** |
| paper exact, ink raised to `ink OR paper` | **no** | one blit, ~0.8 ms | rejected |
| ink exact, paper lowered to `ink AND paper` | **no** | one blit, ~0.8 ms | rejected |
| `OSAPI_GFX_FILL` in paper, then `OSAPI_ICON_DRAW` two cells at a time | yes | 20 calls, **~134 ms** (PERFORMANCE.md Set 84's 6.7 ms a call) | rejected |

**Both approximations were drafted and both are overruled**, and the reason is
worth keeping because the first draft of this document argued for one of them.
The argument was that `ink OR paper` can never equal `paper` on the refused
branch, so the glyph can never vanish into its own background — which is true,
and which answers the wrong question. **A terminal handed an attribute byte
draws that attribute or it draws a different picture.** There is no
degraded-but-honest rendering of a colour the way there is of a shape: the user
cannot tell an approximation from a board that sent those colours, so the
failure is silent and looks like the board's fault. Getting the arithmetic
right about a wrong colour does not make it a right colour.

Between the two paths that ARE exact: the kernel change costs one refused run
about 2.4× an accepted one; `BLIT4` costs it **about seventy times** an
accepted one (~106.9 cycles a pixel against the 1bpp emit's ~12.5 clocks a
*byte*, i.e. ~1.6 a pixel). So the kernel is where it belongs, and `BLIT4` is
the fallback rather than the design. Both figures are arithmetic off
PERFORMANCE.md's own numbers and are **not measured**.

**WHAT LANDED (found).** The split fit: **67 bytes of `.cold` and 3 of `.bss`**
(`tools/kernsize.py` before and after), so the image rung's headroom went
127 → 124 and the cold rung's 221 → 154 and neither was crossed. It is under a
hundred bytes because lifting a refusal deletes the refusal — `.penno` and
§5.4.2.4's eleven-instruction unwind both came out, and `.pen` has one answer
now. `tests/telpen.py` is the gate: six pairs, four of them refused before,
every cell rendered on the HOST out of the guest's own glyph table and compared
against what the card rasterised — **0 differing pixels of 2,944 on each**.
The BLIT4 fallback was never reached and the row that would have taken it does
not exist; what a package still does on a `CF = 1` is §70.8.2's degrade, which
is one `OSAPI_FONT_RUN` for the whole row and is there for `kern_small`.

**The budget is the constraint and it is tight.** `tools/kernsize.py` reports
**127 bytes left in the image rung**, and a rung crossing is 512 bytes of every
machine's RAM — CLAUDE.md makes that a decision taken with whoever asked for
the feature, never a build fix. If the split does not fit in 127 bytes it does
not land, the caller takes the `BLIT4` fallback, and **it does not take an
approximation**. §70.8.3 says so in the contract so that a future reader cannot
reach for the cheap wrong answer when the budget bites.

The masked `ICON_DRAW` path is exact and is a redraw a person watches happen —
a regression against PERFORMANCE.md Part 5's standing budget, not a neutral
trade — so it is out on arithmetic rather than on taste, and it needs a fill
nothing else needs.

### 3.3 The dirty range had to become a bitmap

§70.4 chose a range on a stated assumption — *"terminal output is sequential"*
— and cursor addressing is the thing that assumption excludes. A board writing
row 3, then row 20, then row 3 spans eighteen rows of which sixteen are clean:
**~50 ms of drawing to change two rows**, every time. Four bytes and
twenty-five bits is the whole fix, and both renderers consume it, which is what
§70.6 already required of the range.

### 3.4 Where the glyphs come from

`OSAPI_FONT_GLYPHS` was rejected as the source, and it is the SDK's own answer
to "where do I get letters", so the reason has to be good: **a `make FONT=`
kernel replaces the system face**, and a board's box-drawing character is not a
design choice this package may inherit. `apps/artful` records exactly this
hazard from the other side. So the ROM is the source, `int 10h AX=1130h BH=3`
where the BIOS has it and `F000:FA6E` where it does not, which is
`kernel/font.inc`'s own probe and `kernel/splash.inc`'s.

**160 glyphs are shipped and 96 are not**, which is a split on a fact:
32..127 are ASCII and every ROM agrees, while 0..31 and 128..255 are CP437's
own and a clone is free to differ. 1,280 bytes of package image.

**Clean-room, and the shades are geometry.** 176..223 are drawn by
`tools/cp437font.py` from their definitions — a 50% shade is a checkerboard, a
box corner is two runs meeting — and everything else is eight lines of ASCII
art authored in the tool, which is a font a person can read in a diff. No
third-party font file is fetched or copied.

### 3.5 CRC-16 only, and no table

`CANFC32` is not advertised. A CRC-32 table is **1,024 bytes** and the untabled
form is 32-bit arithmetic on an 8086, and the link is already checksummed twice
below Zmodem (TCP's and Ethernet's).

And the CRC-16 is **bitwise, with no table either**, which is the less obvious
half:

| | |
|---|---|
| bitwise CRC-16, 8 shifts a byte | ~80 cycles ≈ 16.8 us a byte |
| that as a rate | **~59,500 bytes a second** |
| what the cable delivers (PERFORMANCE.md Set 39) | **3,741 bytes a second** |

A 512-byte table buys throughput the transfer cannot use. Estimates from
instruction counts, **not measured**.

### 3.6 NAWS tells the truth

A client that reported its *viewport* would have the host wrap lines where the
buffer is not going to wrap them, so every line of art after the first would be
in the wrong place. Reporting 80x25 always is only possible because the buffer
is 80x25 always — which is §3 of this document arriving back at TELNET-PLAN
§1's sentence.

The tempting middle — report the viewport when the window is narrow so a board
sends narrower menus — was rejected: a board does not resize its art, it
chooses a different *screen*, and there is no board that draws for 74 columns.

### 3.7 The cancelled dialog

`OSAPI_FILE_DLG` has no cancel callback: §38.6 states it and `fdlg_close` is
where it is true. Four ways out were looked at.

| | verdict |
|---|---|
| re-issue `OSAPI_FILE_DLG` on a timer and read CF | **no** — CF=0 means the old one is gone *and this call just opened a new one*, so the probe is the bug |
| a new SDK slot, "is a modal dialog up" | **no** — §20.8 would take it, but a slot added for one package's convenience is exactly what CLAUDE.md's index exists to prevent, and the answer below needs none |
| **a `W_PAINT` while `[tz_dlg]` is set is the cancel** (taken) | the dialog is modal and covers this window, so no paint reaches it while the dialog is up; destroying it by either exit exposes this window. On a commit `fdlg_commit` calls back *before* the paint is dispatched, so the flag is already clear; on a cancel it is not |
| a timeout alone | **kept, as the backstop only** — 60 seconds, for the paint that never comes rather than for the user who is thinking |

**This is the most fragile thing in §70.11 and it is written down as such.** It
depends on an ordering that `fdlg_commit` documents and does not promise, and
`tests/telzm.py`'s third run is the gate for it specifically.

### 3.8 Auto full screen on the first CSI: rejected

It is tempting — a board's first act is a `CSI 2J`, and a windowed 640-pixel
screen cannot show all eighty columns. It was rejected on two grounds. **§53's
bracket takes the whole machine**, so a screen mode change that the user did
not ask for is a desktop that vanished; and **§11.2.1 pins the fullscreen key
in both directions**, which is a contract about the user deciding. The Session
menu item and `^]` are one keystroke, and the About panel names it.

### 3.9 The keyboard-mouse takes the arrow keys, and that is not fixed here

§9.6 gives Home, Up, PgUp, Left, Right, End, Down, PgDn, Ins, Del and Space to
the pointer on a machine with no mouse, and *"an application does not see
them"*. A board needs every one of them. **ScrollLock is the documented escape
hatch and is the whole answer this work gives**: the About panel says so.

A per-window opt-out — a window flag that says "I take typed text, leave the
arrows alone" — is the right fix and belongs in §9.6 rather than in a package,
alongside §11.2.1's existing exemption of exactly this class of app from the
fullscreen key. It is **not in this work** because it is a kernel change with
its own gate, and because the machine class it matters on (no mouse at all)
is the one this project's own test machines do not have.

## 4. Deferred, with the arithmetic

### 4.1 Zmodem UPLOAD

Out of scope as asked, and the cost if it comes back: a sender is a second
state machine of about the receiver's size (~1.5 KB of image), needs
`OSAPI_FILE_READ_AT` (0x0358) chunking with the same cluster rule read the
other way, and needs a file-picking `OSAPI_FILE_DLG` in Open mode. **The hard
part is not the protocol, it is `ZRPOS`**: a sender must be able to seek
backwards to any offset the receiver names, so the read chunking cannot be a
one-way walk. Nothing in the receiver is shaped to make it harder.

### 4.2 CRC-32 (`CANFC32`) and ZBIN32 headers

1,024-byte table plus 32-bit shifting per byte, for zero detectable errors on a
link with two checksums under it. A `ZBIN32` header is refused with `ZNAK`,
which every sender handles by falling back — that is what the negotiation is
for.

### 4.3 Scroll regions (DECSTBM, `CSI r`)

Two more words of state (`te_top`, `te_bot`) and a bound on every scroll, LF,
IL, DL, SU and SD. Cheap in bytes. Deferred because **boards do not use them**:
DECSTBM is a VT feature and the ANSI-BBS drivers boards were written against
(ANSI.SYS, and the ANSI drivers in every terminal package of the era) never
implemented it, so a board that sent one would be talking to a terminal it has
never met. `CSI r` is consumed and ignored by §70.9.3's unknown-final rule,
which is the correct behaviour for a sequence this does not implement.

### 4.4 A horizontal scrollbar in the windowed view

§70.5's accepted trade is that a narrow window costs the right of every line
with no way to reach it. A scrollbar would need: a control (about 300 bytes
with `os88ui`), a `[te_vleft]` word, a second term in every blit's x, and a
second term in the About panel's fill and the scroll blit's rect — both of
which `con_wpx` exists to keep to one opinion. **The reason it is not worth it
is not the bytes**: full screen shows all eighty columns, it is one keystroke
away, and it is where a board is used. A scrollbar would be the affordance for
a mode nobody should be in.

### 4.5 RIPscrip

Boards that use it are vanishingly rare and it is a vector graphics language
with its own parser, its own font set and its own mouse regions — a package
rather than a section. `ESC ! ` opens a RIP command and §70.9.1's `ESC`
fall-through swallows the introducer, so a RIP board degrades to its ANSI
fallback, which is what RIP was designed to do.

### 4.6 Auto full screen, and the keyboard-mouse opt-out

TELNET-PLAN §3.8 and §3.9.

## 5. Two defects the tree already had, verified before anything was written

Both are in `apps/telnet/tetxt.inc` and both are recorded in §70.8.8 with the
fix. What was actually checked, in the code:

**1. Full screen did not scroll.** `te_tx_owed` ends `mov word [con_scrl], 0`
under the comment *"a text row change IS the scroll here: the rows are
re-emitted."* Only rows in the dirty range are re-emitted, and `te_scrollck`
marks exactly one — `mov bx, CON_ROWS - 1`, the row it opened. So the buffer
scrolled and VRAM was told about the last row: **rows 0..16 kept pre-scroll
text for the rest of the session** and the bottom row was overwritten again and
again. A board's output is one long scroll, so the whole feature was legible on
one line. Not reproduced on the glass — it is read off the code, and
`tests/telansi.py`'s full-screen arm is where it becomes a gate.

**2. `te_show` parks the socket's owner for the whole bracket.** `te_show`'s
first instruction is `call OSAPI_GFX_LOCK`; its `cmp byte [te_txm], 0` is
eleven instructions later. §53.6 says the bracket holds the gfx lock from
before `fsx_run` to after it returns, `kernel/fsx.inc` confirms it (the file
contains exactly one `gfx_lock` reference and it is a comment about the
caller's hold), and §53.2 is binding about the consequence: *"a kept worker
feeds data and never takes the gfx lock… it parks safely if it tries, but for a
feeder, parking is death by another name."* `gfx_lock` is a yield-spin on a
byte that cannot change until the bracket exits.

So the file's own comment — *"[te_txm] is set BEFORE `OSAPI_FSX_RUN`, so the
kept worker skips its very first turn"* — describes something that does not
happen. The first byte to arrive after entering full screen marks a row,
`te_owed` answers true, `te_show` parks, and **the session is frozen until
`^]`**. Moving the test ahead of the lock costs one compare per windowed pass.

The general rule it encodes is in §70.8.8 and is not `te_show`'s alone: a kept
worker's every path to a drawing slot must be gated on `[te_txm]` **before**
the gate, not inside it.

## 6. What it costs the package

`tools/os88pkg.py`'s budget is `APP_MAX_SIZE` = 0xF000 = **61,440 bytes for
image + bss together**, and it prints `image=… bss=…` on every build. Today
`build/telnet.bin` reads **image 4,719, bss 1,935 — 6,654 of 61,440**.

The estimate afterwards, item by item. **These are arithmetic, not a
measurement**; the real numbers come off `os88pkg`'s line when the code exists.

| item | image | bss |
|---|---|---|
| today | 4,719 | 1,935 |
| `con_scr` 80×25×2, replacing 64×18+1 | | +2,847 |
| `con_glyf`, the 256-glyph runtime table | | +2,048 |
| `con_band`, one composed row | | +640 |
| Zmodem staging, two 4 KB halves | | +8,192 |
| `te_txb` 64 → 256, plus `te_pnd` 32 | | +224 |
| `con_drb` and the new state bytes | | ~+40 |
| `tecp437.inc`, 160 shipped glyphs | +1,280 | |
| `teansi.inc` | ~+1,500 | |
| `tezm.inc` | ~+1,800 | |
| **estimated total** | **~9,300** | **~15,900** |

**What it actually reads, after §70.8 and §70.9/§70.10** — `os88pkg`'s own
line, the two waves that have landed:

| | image | bss | total |
|---|---|---|---|
| the estimate above, waves 2 and 3's share | ~7,500 | ~7,700 | ~15,200 |
| after §70.8 (the screen, both renderers, the glyphs) | 7,052 | 7,554 | 14,606 |
| **after §70.9/§70.10 (the parser, the keys, the queue)** | **10,235** | **7,821** | **18,056** |
| after §70.11 (the Zmodem receiver) | 14,337 | 16,254 | 30,591 |
| **...and the w4 review's fix pass — FINAL** | **15,079** | **16,299** | **31,378** |

The estimate was **good to about four per cent on bss and nine on image**,
which is worth saying because the estimate is what the whole plan was sized
from: `teansi.inc` came in at 2,494 bytes against ~1,500 guessed, and the
difference is almost exactly §70.10's negotiation, key table and queue, which
this table put in no row of their own.

**And the FLOPPY is the budget that bit, not the validator.** 18,056 of 61,440
is 29% and never in question; what refused the build was `os88disk.py` on the
360KB geometry, twice — the system disk had two clusters free and the apps disk
had **none at all**. §24.3.1 is the account and the arithmetic. Nothing in §6's
estimate could have caught it, because this table measures the package and the
constraint is the disk it rides on with sixteen other packages, ten typefaces
and the whole driver set.

**31,378 of 61,440 — 51%, and that is the FINAL figure.** The estimate said
~25,200 and the receiver came in 6,000 over it, all of it in `tezm.inc`: ~1,800
was guessed and the file is about 4,000 bytes of code, because the estimate had
no row for the 8.3 mangle, the 32-bit decimal formatting the progress line
needs, the progress panel itself or the diagnostic counters §70.11.6 kept.

Comfortable against the validator, and **the thing to watch is still not the
validator**: the package region is a **heap claim**, so the real limit is
whatever the heap has contiguous, and a 30 KB claim on a 256 KB machine is a
different question from a 6.6 KB one. `os88pkg`'s own comment says so. Not
measured; the launch on `vm/xt` with 256 KB is where it becomes a fact.

**AND THE `kern_small` SENTENCE IS NOW ARITHMETIC THAT DOES NOT FIT.** §70.8.3
kept the blit-refusal degrade for one reachable case — a user hand-copying
`TELNET.O88` onto a `kern_small` system disk, where `OSAPI_GFX_BLIT1` is a
`stc`/`ret` stub. **31,378 against a heap of about 28 KB means that copy is
most likely refused at the claim and never reaches the renderer at all.** It
is a SUBTRACTION and not a run — nothing in this tree boots a `kern_small`
kernel with this package beside it, and the Makefile's `SMALLOMIT` is what
stops it — so the honest statement is that the degrade is compile-tested only
with no reachable case left in it. It stays anyway: the heap figure is a
configuration a fork can change, and forty bytes is a worse thing to save than
a documented refusal.

**And there is a second budget, which is the kernel's and is nothing to do with
this one.** §5.4.2.2.1's Map Mask split lives in `kernel/vga12.inc` and spends
`.text`, so it is measured by `tools/kernsize.py` against the **image rung**:
`image 57,344 +512 (127 left)`. 127 bytes, no rung crossing, and a crossing
costs 512 bytes of every machine's RAM whether or not the machine ever runs a
terminal. That is the one number in this work that is charged to people who
will never use the feature, which is why TELNET-PLAN §3.2 spends as long as it
does justifying it and why §70.8.3 names a fallback for the case where it does
not fit. **To be measured.**

The 8 KB staging is the single largest item and it is the one with an argument
behind it: §77.21 found that **the staging size is the seek count**, and a
smaller buffer means more `OSAPI_FILE_APPEND` commits, each of which is two FAT
flushes and a directory write. Halving it to 4 KB would double the seeks on a
download for 4,096 bytes of a budget that has 30,062 spare — and **the halves
are what the double-buffering is**, so halving the area would also mean the
worker waits for every commit instead of filling the other half through it
(§70.11.3). It is one argument doing two jobs.

## 7. What an implementer will hit

0. **This work contains a KERNEL change and it is the only one.** §5.4.2.2.1's
   Map Mask split is in `kernel/vga12.inc`, under `tools/kernsize.py`'s image
   rung with **127 bytes** of headroom and no rung crossing permitted — a
   budget that has nothing to do with the package's own (TELNET-PLAN §6). Do it
   FIRST and measure it before writing the renderer, because the renderer's
   fallback path (`OSAPI_GFX_BLIT4` per refused run) is only written if the
   split does not fit. `tools/kernsize.py` before and after is the gate, and
   CLAUDE.md's rule applies: a byte costs a byte.
1. **`OSAPI_FILE_DLG` gives back a NAME and nothing else.** No path, no drive.
   That is correct and is `fdlg_home_save`'s doing (§38.10), but it means the
   destination is "wherever the dialog was" and there is no way to read it
   back. `OSAPI_FILE_HERE` reports it if the status line wants to say it.
2. **`OSAPI_FILE_DFREE` answers SECTORS per cluster, not bytes.** `BX × 512`,
   and every §18.4.4 chunking loop needs the multiply.
3. **The cancel inference (TELNET-PLAN §3.7) is the riskiest thing here.**
   Write `tests/telzm.py`'s cancel run first.
4. **`te_promise` must drop `OSAPI_SAVEU_1BPP` per adapter**, not once at
   launch — §11.96.17 re-states the depth claim on every call, and a window can
   move to a display of another depth (§39.18.2).
5. **The kept worker must not reach a drawing slot at all in full screen**, and
   the `[te_txm]` gate now has to come before every one of them, not just
   `te_show`'s.
6. **The RX buffer becomes a queue.** `[te_rxn]`/`[te_rxi]`, and a pass that
   could not finish it issues no new `NETV_RECV`. Both the whole-message TX
   rule (§70.10.3) and Zmodem's staging back-pressure (§70.11.3) need it, and
   neither works without it. **Landed in wave 3**, and it bought a third thing
   nobody predicted: `[te_rxi]` advances BEFORE the byte acts, which makes the
   stream offset the parser publishes the offset just PAST the byte — exactly
   what `ansisim.feed()` returns, so §70.9.6's handover offset is one number
   two implementations compute and `tests/telansi.py` compares.
7. **`SPEC.md §74.1`'s slot table is stale** — it lists `OSAPI_WM_WAKE` at
   0x0428 and `OSAPI_WM_ONWAKE` at 0x0430, and `kernel/kernel.asm` has them at
   **0x0450** and **0x0458** (the note at that table's definition records the
   renumbering). `apps/os88api.inc` is authoritative. Fixing §74.1 is not this
   work's to do and is flagged here so the next reader does not copy the wrong
   numbers.
8. **`apps/telnet/telnet.asm` cites §11.2.1 for "a zero is a bare scan code"
   and §11.2.1 is the FULLSCREEN KEY.** §27 is where that rule is actually
   stated. §70.10.2 corrects it; **the source followed in wave 3** — the
   citation was in `te_txraw`'s header, which the key table replaced, and the
   remaining §11.2.1 in `tetxt.inc` is the RIGHT one (the `F` contract and its
   exemption for an app taking typed text).

9. **A byte no renderer reads is a byte that goes stale in silence.** Two of
   them were declared before any code existed — `te_ul` for SGR 4 and `te_satr`
   for the attribute `ESC 7` saves — and both are gone (§70.9.4, §70.9.3). The
   design's own contract is the 4,000 bytes of `con_scr`; a flag published
   beside it bought the gate nothing and would have been believed by whoever
   next added a rule about underline.

10. **The 360KB floppy is a budget this plan never costed.** §6's table sizes
    the PACKAGE against `APP_MAX_SIZE` and answers 29%; what refused the build
    twice was `os88disk.py` on a 354-cluster disk that carries sixteen other
    packages, ten typefaces and the whole driver set. §24.3.1 is the account.
    A plan for a package that ships on the small geometry should cost the disk
    as well as the claim, and this one did not.
