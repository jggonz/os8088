# WORD-PLAN.md — Microsoft Word 1.1A for os8088

Design record for `apps/word/` — a faithful native reimplementation of Microsoft
Word for Windows 1.1a ("Opus") as an os8088 package, on its own dedicated
floppy, with `make xt-word` / `make 386-word` 86Box machines that put the Word
disk in B:. The UI definition (menus, ribbon, ruler, status bar, dialogs,
shortcuts) is mined from the Computer History Museum source release of Opus;
every menu string below is verbatim from `Opus/resource/menus.cmd`.

Why a reimplementation and not a recompile: Opus is pcode built with a
proprietary CSL compiler against the Windows 2.x API. None of that exists here.
What CAN be ported faithfully is the thing a user actually touches — the exact
menu structure and wording, the ribbon and ruler, the keyboard map, the status
bar, the formatting model (CHP/PAP), and the document-file identity — running
as real 8086 code against the os8088 SDK.

The binding contract is SPEC.md §65; this file is the reasoning behind it.

## 1. What Word 1.1A on an 8088 honestly is: Draft view

os8088 has exactly one font: fixed-pitch 8×8, chars 32..126, no styles in the
renderer (§6). Word 1.1a itself shipped a mode for exactly this situation —
**View > Draft**: fixed-pitch rendering where character formatting is shown
synthesized, layout is simplified, and editing is fast. That is the mode this
port lives in, and the precedent table of §61.5 says how styles are drawn:

| attribute | rendering |
|---|---|
| bold | glyph double-struck 1px apart (second pass is transparent `font_str`) |
| italic | sheared glyphs (rows 0..3 shifted +1px), pre-rendered at init into a 4bpp table, drawn per run with one `gfx_blit4` from a row staging buffer |
| underline | hline at cell row 7 across the run |
| word underline | hlines under non-space spans only |
| double underline | hlines at cell rows 6 and 7 |
| small caps | lowercase case-mapped to caps in the row buffer (draft approximation) |
| hidden | omitted from layout (shown dotted-underlined when Show ¶ is on) |
| justified | records the attribute; renders as left in draft view (as real draft did for line breaks) — center/right DO render, offset rounded down to 8px to keep the byte-aligned fast path |

A pre-rendered italic table costs 95 glyphs × 8 rows × 4 bytes = ~3KB in a
claim; an italic run is staged into a per-row pixel buffer and lands as ONE
`gfx_blit4`, so styled text stays priced in calls, not pixels.

## 2. The chrome is the app's, not the kernel's

`MENU_APPMAX` = 5; Word's bar has nine menus. So the Word window draws its own
chrome — which is what the real product did inside its own frame. Content
layout, top to bottom (heights in px; every strip togglable per View menu):

```
menu bar   14   File Edit View Insert Format Utilities Macro Window Help
ribbon     16   Font:/Pts: combos + B I K | U W D | sup/sub | ¶   (View > Ribbon)
ruler      18   Style: + align×4 + spacing×3 + open/close + tab×4 + inch scale
text       *    left margin 8, own vertical scrollbar (16 wide) at right
status     12   Pg Sec n/n At Ln Col + CAPS NUM OVR       (View > Status Bar)
```

The kernel menu bar carries only the app name (About + Close) — no
`OSAPI_MENU_SET` set is registered; the in-window bar is the menu system.

Dropdown menus draw over the content and are torn down by repainting the rows
they covered (a transient repaint, priced and acceptable). Interaction is both
Windows-style (click opens, click fires) and press-drag-release; `Esc` closes;
`Alt+mnemonic` opens a menu from the keyboard, a second mnemonic letter fires
the item. Disabled items draw with the disabled pen (`OSAPI_GFX_PEN` CF=1) so
they dither correctly at 1bpp. Menu wording, order, separators and the
right-justified shortcut captions (`Alt+BkSp` on Undo, `F5` on Go To…) follow
`menus.cmd`/`SetBcmMenuKeys` exactly; features that do not exist here (Print,
Spelling, Macro…) are present and greyed — §47's rule: grey a fact, and the
About box says what draft view on os8088 carries.

## 3. Document model — Word's own, shrunk to fit

Three heap claims (§50.3), all made/regrown on the UI task:

- **Text**: flat byte array, paragraph mark = 13, tab = 9, printable 32..126.
  Grows 1KB at a time from 4KB, ceiling `WD_MAXKB` = 30KB (16-bit staging
  arithmetic stays exact; a 30KB draft document is ~15 dense pages).
- **CHP**: one attribute byte per character, mirroring every gap open/close the
  text buffer performs. Bits: 0 bold, 1 italic, 2 underline, 3 word-ul,
  4 double-ul, 5 small caps, 6 hidden, 7 reserved.
- **PAP dictionary**: paragraph formatting lives ON THE PARAGRAPH MARK — the ¶
  mark's CHP byte is an index into a 256-entry dictionary of unique paragraph
  formats (4 bytes each: align/spacing/space-before packed byte, left indent,
  first-line indent (signed), right indent — indents in character cells,
  1 cell = 1/10" at pica pitch, matching the ruler's inch scale). Entries are
  deduplicated on insert and live for the session; a 257th unique format is
  refused with a toast. Deleting a ¶ merges the paragraph into the next one's
  format — exactly Word's semantics, and it makes undo, cut/copy/paste and
  split/join fall out of the text+CHP machinery with no third structure to
  keep synchronized.

Typing state: current CHP byte (applied to inserted chars) + inherited PAP for
new paragraphs. Selection formatting applies over the span; the ribbon/ruler
toggle states reflect the caret (or the selection's common state).

## 4. Engine: derived from Note Pad, not rewritten

The text engine transplants `apps/notepad/notepad.asm`'s proven architecture
(SPEC.md §27) wholesale, prefix `wd_`: the one-walk/many-queries layout pass,
row signatures + damage ranges, the space-padded row buffer flushed as one
opaque `font_run`, blit scrolling with description shift, decimating checkpoint
index, append fast path, XOR selection, 5-deep undo, the worker paying
wrap/height debts (hired at the first paint here, not lazily: the CAPS/NUM
lamps need its poll and File > Close needs a task to die on). Deliberate
deltas:

- **Styled flush**: a row flush emits one background `font_run` for the plain
  span plus overlay passes per styled run (double-strike, rules, italic blit).
  Row signatures fold the CHP byte with the char so a formatting change dirties
  exactly the rows it touched.
- **Variable row advance**: line spacing single/1.5/double = 8/12/16 px row
  advance, plus 8px before an "open" paragraph. A per-visible-row height array
  rides beside `wd_rows`; blit scroll deltas come from it. This is the one
  structural change to the walk and the riskiest part of the port.
- **Search**: the regex engine is dropped (Word 1.1 had none); literal search
  gains Whole Word and Match Upper/Lowercase options, plus Replace with
  Confirm Changes — the real dialog's feature set.
- Undo records carry text+CHP blobs in parallel arenas; PAP needs nothing
  (it rides the ¶ marks' CHP bytes).

## 5. Dialogs

Modal in-window panels (saved under by repaint-on-close), os88ui buttons/
check boxes/radios, own edit fields. From the Opus `.des` sources: **Format
Character** (the seven check boxes; Position/Color greyed), **Format
Paragraph** (alignment radios, indents in inches, spacing, tabs note),
**Search** / **Replace** (Search For / Replace With, Whole Word, Match
Upper/Lowercase, Direction, Confirm Changes), **Go To** (page number),
**About** — plus Word's "Do you want to save changes to <DOC>?" Yes/No/Cancel
on Close/New/Open/Exit with a dirty document. File Open/Save As use the
kernel's Standard File dialog (§38) — the OS convention, and the completion's
free size word is the refusal gate.

## 6. Files

Native format, extension `.DOC`, using Word's real magic: header word
`0xA59B` (`wIdent` from `Opus/wordtech/file.h`), then an os8088-native FIB:
version, text length, CHP length, PAP entry count, flags; then text bytes, CHP
bytes, PAP table. Assembled into a staging claim and written with one
`OSAPI_FILE_WRITE`. Open sniffs the magic: native loads everything; anything
else imports as plain text (CRLF folded, default format) — so README.TXT files
open too. Package association block claims `DOC`, so double-clicking a
document on the desktop launches Word with `OSAPI_ARG_FILE`.

`tools/os88doc.py` generates the shipped sample (`WELCOME.DOC`) from a marked-
up text source at build time, deterministically — the disk carries a document
that actually exercises the formatting.

## 7. The disk and the machines

Frotz precedent (§61.9) exactly: `WORD.O88` does NOT ride the apps disks —
`make worddisk` builds `build/word.img` (1.44MB), `word720.img`, `word360.img`,
each with `WORD.O88` + `WELCOME.DOC` in the root and an empty `DOCS\` folder
(where the file dialog lands saves). On-demand targets; `all` untouched.

- `make xt-word` — IBM XT, 4.77MHz 8088, 640KB (the claims for text+CHP+
  staging+undo are what the memory is for), 360KB system floppy in A:, the
  720KB Word disk in B: (3.5" DD, the same period-plausible drive xt-z uses).
  No sound card — Word makes no sound; the plain-machine precedent applies.
- `make 386-word` — 386DX/25, two 1.44MB drives, B: = `word.img`. AT-class:
  first boot wants CMOS ("EXIT FOR BOOT" once, then `vm/386-word/nvr/` is
  written).

Both targets `$(UNPROTECT)` their cfg — 86Box's `wp://` rewrite would turn
every document save into FERR_WPROT, reading as a Word bug.

QEMU testing: `make test TESTAPPS=build/word.img` swaps B:; drive with
`tools/mouse.py`/`tools/qmp.py`, verify with `tools/shot.py --crop --zoom`,
and look at 1bpp adapters (`VIDEO=cga`, `VIDEO=herc`) before calling any
drawing change done.

## 8. What is in, what is greyed

Working: typing/caret/selection/click+drag, cut/copy/paste (system clipboard),
undo, word wrap, scrolling, character formatting (bold/italic/underline/word/
double/small caps/hidden) via ribbon+Ctrl keys+dialog, paragraph formatting
(alignment, line spacing, open/close space, left/first/right indents) via
ruler+Ctrl keys+dialog, default tab stops, Search/Replace/Go To, status line
(Pg/Sec/page-over-total/At/Ln/Col + EXT CAPS NUM OVR lamps, page = 54 lines;
the At field drops first on a window too narrow to show every cell), F8
extend mode, overtype (Ins), Show ¶,
View toggles (Ribbon/Ruler/Status Bar), New/Open/Save/Save As/Close/Exit with
save-changes prompts, .DOC format + association, About.

Present-and-greyed (the menu is the real menu): Print family, Spelling/
Thesaurus/Hyphenate, Macro menu, Outline/Page view, fields, footnotes, tables,
styles beyond the dictionary, Paste Link, Summary Info, Glossary — each greyed
as a fact of this platform, per §47.

## 9. Keyboard map (subset of `keys.cmd` that exists here)

Ctrl+letter formatting: C-B bold, C-I italic, C-U underline, C-W word-ul,
C-D double-ul, C-K small caps, C-H hidden*, C-Space reset char; C-L/C-C/C-R/
C-J alignment; C-1/C-5/C-2 line spacing; C-O/C-E open/close space before;
C-N/C-M indent/unindent, C-T hanging indent, C-G unhang; C-F/C-P/C-S are
ribbon/ruler box focus in the real map — here they open the matching dialogs.
(*int 16h folds C-H/C-I/C-M into BS/Tab/CR; those three arrive via the Format
Character dialog instead — the same collision Note Pad documents.)

F-keys: F1 About/help note, F3?—no (glossary), F4 repeat search, F5 Go To,
F8 extend selection (status EXT), F12/S-F12/C-F12 Save As/Save/Open; Ins
overtype; Del/BkSp; S-Del cut, C-Ins copy, S-Ins paste; A-BkSp undo; Esc
cancels menus/dialogs. Everything else from keys.cmd that names a greyed
feature stays unbound.

## 10. Budgets

- Image+bss ≤ 0xF000 (60KB, §20.1). Note Pad is 15.9KB for the whole engine;
  chrome+formatting+dialogs is budgeted at ~2× that. `OS88_IMAGE_END` enforces.
- Redraw budget: unchanged from Note Pad's — a keystroke letters ~2 cells, a
  formatting toggle repaints the rows it changed plus one ribbon button, a
  menu open/close repaints the covered band once. Nothing repaints more than
  it changed (PERFORMANCE.md Part 5 is the bar).
- Claims: text 30KB + CHP 30KB + PAP 1KB + save staging ~64KB transient +
  italic table 3KB + undo arena ≤16KB — comfortably inside a 256KB XT's
  ~167KB heap for mid-size documents, with `OSAPI_MEM_AVAIL`-gated refusal
  (grey the fact) beyond that.

## 11. The second pass: what else in Opus turned out to be portable

Section 1 said the port is of "the thing a user actually touches", because
Opus is pcode against the Windows API and none of that exists here. That is
still true of the product as a whole — and it left four things on the table
that are neither pcode nor Windows, only C over a document:

| Opus | what landed | where |
|---|---|---|
| `wordtech/file.h`, `fkp.h`, `doc.h`, `prm.h`, `props.h`, `wordwin.h` | the REAL `.DOC` — FIB, FKP pages, bin tables, STSH, plcfsed, DOP, and the piece table on the read side | `apps/word/wddoc.inc`, SPEC.md §68.4 |
| `RTFOUT.C` / `RTFIN.C` | RTF out and in | `apps/word/wdrtf.inc`, §68.8 |
| `search.c`'s `SetSpecialMatch` | the pattern language: `?`, `^p`, `^t`, `^w`, `^nnn`, and `^m`/`^c` in the replacement | `apps/word/wdutil.inc`, §68.7 |
| `sort.c`, `renum.c`, `toc.c` | Utilities > Sort / Renumber and Insert > Table of Contents | `apps/word/wdutil.inc`, §68.9 |

Three things are worth writing down about how that went.

**The file format needed a second implementation to be worth anything.**
There is no Word here to open the output with, and a format that only
round-trips through the app that wrote it is a private format with Opus's
magic on it — which is exactly what the first version was. So
`tools/os88doc.py` writes the real format from the host side and
`tools/wordfmt.py` reads it, sharing no code, both from the Opus headers;
`make wordcheck` runs the pair and diffs the result against the markup the
document was generated from. That is what caught the FIB's `fcPlcfbteChpx`
offset (160, not 172 — the pairs are `94 + 6n` and I had miscounted `n`), a
CHPX written without its `cb` byte, an FKP whose `rgb` array was stored at
the offset the CURRENT run count implied rather than the final one, and a
PAPX pad byte left holding the previous record's sprm. None of those four
would have shown up as a wrong pixel; all four make the file unreadable to
Word. The two implementations now agree byte-for-byte on `WELCOME.DOC`.

**`mul` writes `DX`.** Four separate defects in this work were one register
discipline mistake: a 16-bit multiply or divide writes `DX:AX`, and in each
case `DX` was carrying something — the packed PAP byte, the "this control
word had a parameter" flag, the decimal digit being accumulated, the TOC
entry's level. Three of the four are invisible until you look at the output
closely (a lost space-before, `\b0` read as `\b`, an indent taken from a
division remainder); the fourth wrapped every table-of-contents line to one
character. The related trap is `pop ax` after a byte fetch: `call wd_docb2`
answers in `AL`, and preserving `AX` across it throws the answer away. Both
are the sort of thing a comment at the call site is worth more than a rule.

**A command that clobbers `SI` cannot be followed by a repaint.** Sort,
Renumber and Table of Contents all use `SI` as scratch, and `wd_redraw`
wants the window there. The document was correct and the screen was not,
which reads exactly like the command doing nothing — and the way it was
found was saving the file and parsing it on the host, not looking harder at
the screen.
