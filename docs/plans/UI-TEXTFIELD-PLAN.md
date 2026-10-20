# A SHARED TEXT AREA

**(This was "a shared text field, and a shared text area". The FIELD half was
wrong: `apps/os88line.inc` already is one. See §1.)**

**STATUS: BRIEF ONLY. NOTHING IS DESIGNED HERE AND NOTHING SHOULD BE BUILT
FROM IT.** It exists so that the next person to want a text field finds the
question already asked, the carriers already counted, and the one decision
that must not be taken casually already written down. The work is a study with
a survey in front of it, and the survey has not been done.

It was opened while planning the DOS box's arguments-and-environment page
(docs/plans/DOS-EXEC-PLAN.md). That page needs a single-line field and decided
to **copy** one rather than invent a shared control, deliberately — see §4.

---

## 1. CORRECTION: the field already exists, and this plan's first revision
## missed it

**`apps/os88line.inc` is a shared one-line text field**, the second shared
include in `apps/` carrying code, and it is used by `apps/browser`,
`apps/telnet` and `apps/ftpd`. It has the whole control: `os88line_draw`,
`_caron`, `_caroff`, `_key`, `_hit`, `_click`, `_set`, a block you declare
(`OS88LINE_SZ`) so a window may have two, and a documented split of
responsibility — `os88line_key` answers CF=1 for *"that keystroke is yours"*,
so Enter, Tab and Escape go back to the caller.

It has also already solved the thing this plan would have had to: its
`os88line_pen` rounds the text pen **up to a multiple of 8** so `font_run`
takes its single-store path, and its comment carries the measurement — a
26-character URL flashed **246 transient pixels over ~18 cells** per keystroke
before that, because `WF_SNAP` makes the content origin 8-aligned and every
caller's own small inset then pinned the pen at 6 mod 8 for ever.

**So the first revision of this document was wrong in its first line**, and
the way it went wrong is worth keeping: it counted carriers by grepping for
`caret` and found twelve, which are `apps/browser`, `apps/ftpd`,
`apps/notepad`, `apps/texpad`, `apps/word`, `apps/sheet`, `apps/scribe`,
`apps/loom`, `apps/artful`, `apps/paint`, `kernel/ctrl.inc` and
`kernel/fdlg.inc`. Most of those are **multi-line editors** — which §2 below
argues at length are a different control — so the count conflated exactly the
two things the rest of the document is about keeping apart. Three of the
twelve are `os88line.inc` callers and were never hand-rolled at all.

**What is left open is the AREA alone**, and the two kernel carriers
(`ctrl.inc`, `fdlg.inc`) which are neither — a kernel file cannot include an
`apps/` header, so whether they *should* share one is a separate question with
a resident-bytes answer.

That is the same shape docs/plans/completed/CTRL-GLYPH-PLAN.md found one
control along, where converging the two check boxes **REMOVED** bytes rather
than spending them - most of what each copy carried was not the control's
logic but the machinery around it. Whether that is true of a field is unknown
and is what §3's survey has to answer.

**But bytes are not the argument, and docs/plans/UI-MENU-ELEMENT.md is the
precedent that already landed.** Word's in-window menu became a shared element
in three waves, and that plan's §0 records the correction worth inheriting
here: the first refusal was written on bytes alone, *"which was the wrong
argument about the wrong unit"*. What `apps/os88ui.inc`'s own header says
instead is the argument for this:

> Saving bytes was never the argument … The argument is that a feature added
> to the button lands in the Standard File dialog, the Control Panel, the
> Timer AND every package at once, because there is one body rather than ten
> that agree by hand. SPEC.md 47's greying rule was fixed FIVE separate times
> in this tree, each as its own bug.

Twelve hand-rolled fields is twelve chances to get §47's refusal, §39.4's
1bpp greying, or the one-cell caret wrong independently - and the glyph
conversion showed that changing a shared body reaches every carrier at its
next build with no per-package work. **That** is what a survey is for, and a
field that came out byte-neutral would still be worth having.

## 1.1 FOLLOW-ON: three carriers still repaint the whole field per keystroke

`os88line_edit` was added to the include while building the DOS box's
arguments row (SPEC.md 96.19.1) and **only that row calls it**. `apps/ftpd`,
`apps/browser` and `apps/telnet` still call `os88line_draw` after every
keystroke, which repaints every visible character: ~18ms a key on a 4.77MHz
machine for a 20-character field, at PERFORMANCE.md's ~900us a glyph cell.

Converting each is a few lines — bank `LN_VIEW` and `LN_LEN` before the key,
pass them to `os88line_edit` after — and it is **the whole argument for a
shared include made good**: the body was changed once and every carrier can
have it at its next build.

What it needs is a look at each on the glass, because a partial repaint that
is wrong leaves ink behind. `ftpdflick` and the browser's own rows are the
gates that already exist; neither can SEE this defect (redrawing an identical
glyph changes no pixel), so each conversion wants the cell COUNT the DOS row
uses, not a pixel comparison.

## 2. The field and the area are NOT one control

This is the thing to not get wrong, and it is why this document exists rather
than a line in somebody's commit message.

A **single-line field** is a rect, a string, a caret index, a key handler and
a draw that touches one cell. **That one is built** — `apps/os88line.inc`, §1.

A **multi-line area** is a miniature text editor — wrap, scroll, a caret that
moves in two dimensions, selection, and a redraw that has to decide how much
of the page a keystroke damaged. **That is 50% or more of Note Pad**, and a
"shared area" that did it all would be Note Pad with a different name.

So the area's design question is not *what does it do*, it is **which of its
features are opt-in**, in the shape `os88ui.inc` already uses for the button
(`OS88UI_NOBTN`) and docs/plans/completed/GFX-EMBEDDABLE-PLAN.md uses for the
graphics library (`GFXE_BAND`, `GFXE_LINE`, `GFXE_POINTS`, `GFXE_WALK`): a
carrier opts into capability, not into the whole thing.

## 3. What the survey must answer before a line is written

One row per carrier, and the questions are the ones that decide whether a
shared body can serve it at all:

1. **What does its caret cost to move?** `apps/ftpd`'s is the one already
   known: `fd_setup_uncaret` puts back the single 8px cell the 1px bar
   covered. Any carrier that redraws a line is a carrier whose redraw changes
   when it converts, which is a behaviour change and not a refactor.
2. **Who owns the keys?** A field inside a window with a menu, a field inside
   a modal dialog and a field inside a full page each get their keystrokes by
   a different route.
3. **What is the string?** Fixed buffer, heap claim, or a slice of a document.
   The last one is what makes Word and TeXPad different in kind.
4. **What does it validate?** A port number, a file name, an IP address and a
   free-text label have four different refusal behaviours, and §47 says a
   refusal names its reason.
5. **Is it on a 1bpp adapter?** Greying rounds to black there (§39.4, §47),
   so a disabled field and a focus indication both have to be looked at on
   CGA and Hercules rather than reasoned about.
6. **How many bytes is each copy?** CTRL-GLYPH-PLAN's number was 414 a copy in
   23 copies, and it is the number that decided that plan. Nothing here is
   decidable without the equivalent.

## 4. Why the DOS box is copying instead of converging

Recorded so it is not re-litigated. The DOS box needs a field NOW and this
study is not started. Converging twelve carriers is a study, a conversion and
a look at every one of them on three adapters; doing it under a wave that is
about something else would be the tail wagging the dog, and doing it badly
would put an eleventh implementation in the tree wearing a shared name, which
is worse than an honest twelfth copy.

**It copies `apps/ftpd`'s**, and the reason is not proximity: FTPD's Setup page
is the same shape (a page of fields in a window), its caret already costs one
cell, and it is the only field in the tree with **rows that keep it honest** -
`ftpdflick` measures the repaint at two cells rather than the page, and
`ftpdfocus` catches a caret left drawn in a field the keyboard no longer
reaches. Copying it brings those two rows along as copies rather than as new
thinking.

When this study happens, the DOS box is therefore a carrier like any other and
its conversion is not a special case.

## 5. What is explicitly NOT decided here

- Whether the field belongs in `os88ui.inc` at all, or in a driver, or in a
  library beside it the way `apps/os88gfx.inc` sits beside `apps/os88ui.inc`.
- Whether the area is ever built. It may be that three carriers need a real
  editor and the other nine need a field, in which case the area is not a
  control and saying so is the finding.
- Anything about size. Every number in this document is a count of carriers,
  never a count of bytes, because no byte has been measured.
