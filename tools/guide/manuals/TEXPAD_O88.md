# TeXPad

TeXPad is a small editor for TeX, the markup scientists and writers use for
papers. You type the source on the left, press `F5`, and the typeset page
appears on the right. TeXPad knows a paper-oriented part of LaTeX: titles,
sections, lists, quotes and ruled tables. When the page looks right, it
writes a PDF or a PostScript file you can print or read on another computer.

![TeXPad with the sample paper: source on the left, page 1 on the right.](shot:texpad-window){full}

## Starting TeXPad

TeXPad is in the `APPS` folder of the apps disk, and at the top of the
Office disk. Double-click **TEXPAD.O88** to start with a new document, or
double-click any `.TEX` file to open it in TeXPad, already typeset. Two
documents to try are in the `MEDIA` folder: [[PAPER.TEX]], a short sample
paper, and [[GUIDE.TEX]], a guide to the markup written in that markup.

You can open more than one TeXPad at a time. The window can be resized.

## The window

Across the top is a row of buttons, then the document's name and the page
you are looking at, such as `PAPER.TEX 1/3`. The source pane is on the left
and the page preview on the right, each with its own scroll bar. The line at
the bottom shows the page setup and how much of the document's 8,190
characters you have used.

Click a pane to work in it. The pane with the keyboard has a black ring
around it.

## Writing the source

Type in the left pane as you would in any editor. Long lines wrap to fit the
pane, and a row that continues on the next one ends with a `>`. The wrap is
only on screen: the file keeps your lines as you typed them.

| Key (source pane) | What it does |
|---|---|
| Arrow keys | Move the caret |
| `Home` / `End` | Go to the start or end of the row |
| `Page Up` / `Page Down` | Move a screenful up or down |
| `Backspace` / `Delete` | Delete before or after the caret |
| `Tab` | Type two spaces |
| `Enter` | Start a new line |

Drag with the mouse to select text. Typing replaces the selection, and
`Backspace` or `Delete` removes it.

## Typesetting

TeXPad does not typeset while you type. Press `F5`, click
**Typeset**, or choose **View > Typeset** when you want to see your changes.
While the preview is older than the source, the top row says **(edit)**.

![The new paragraph is in the source, and the top row says (edit) until F5.](shot:texpad-edit){full}

![After F5 the paragraph is on the page.](shot:texpad-typeset){full}

Opening a document, double-clicking one and **File > New** all typeset
straight away.

## Reading the pages

Click **<** and **>** on the top row to turn pages, or use **View > Prev
Page** and **View > Next Page**. The arrows are grey when there is no page
that way.

Press `Esc` to typeset and move the keyboard to the preview. There the keys
are:

| Key (preview) | What it does |
|---|---|
| `Up` / `Down` | Scroll the page |
| `Page Up`, `Left`, `[` | Previous page |
| `Page Down`, `Right`, `]` | Next page |

![Page 2 of the sample paper, with its table.](shot:texpad-page2){full}

**View > Wider Source** and **View > Wider Preview** move the line between
the two panes.

## The buttons

| Button | What it does |
|---|---|
| **Typeset** | Typesets the source into the preview |
| **Ltr** | Paper size: click to step through Ltr, Leg, A4 and A5 |
| **1pg** | Pages: 1pg (single), Fac (facing) or Opp (opposing) |
| **G+** | Gutter: G0, G+ or G++ |
| **Pad** | Padding: Pad-, Pad or Pad+ |
| **<** / **>** | Previous and next page |

Each of the four middle buttons shows its current setting and moves to the
next one when clicked. The change shows at the next typeset.

## The File menu

| Item | Key | What it does |
|---|---|---|
| New | `Ctrl-N` | Starts a new document from a short template |
| Open... | `Ctrl-O` | Opens a `.TEX` file, using the file dialog |
| Save | `Ctrl-S` | Saves under the current name, without asking |
| Save As... | | Saves under a new name, using the file dialog |
| Export PDF... | | Writes the typeset pages as a PDF file |
| Export PS... | | Writes them as a PostScript file |

A new document is called `UNTITLED.TEX`. Use **Save As...** to give it a
real name. The top row shows the document's name with a `*` after it while
there are unsaved changes.

When there are unsaved changes, **New**, **Open...** and closing the window
first ask **Save changes to** the document: **Save** saves it under its
current name and goes on, **Discard** goes on without saving, and **Cancel**
leaves everything as it is.

**Export PDF...** and **Export PS...** open the file dialog with the
document's name filled in. Type the name for the new file, such as
`PAPER.PDF`, and press **Save**.

![Exporting a PDF: the file dialog asks for a name.](shot:texpad-export){full}

The PDF is version 1.4 and the PostScript is Level 1, so almost any reader
or printer takes them. Text is set in Courier, the same fixed-width letters
you see on screen.

## The Edit menu

| Item | Key | What it does |
|---|---|---|
| Copy All | `Ctrl-C` | Copies the whole source to the clipboard |
| Paste | `Ctrl-V` | Puts the clipboard's text at the caret |
| Cut Line | `Ctrl-X` | Removes the line the caret is on, and puts it on the clipboard |

**Cut Line** works on a line of the file, newline and all, even when the
line wraps over several rows on screen.

## The Layout and Page menus

| Item | What it does |
|---|---|
| Layout > Article | Single pages, the paper style |
| Layout > Book | Facing pages, the book style |
| Layout > Narrow, Normal, Wide Margins | Margins of 0.75, 1 or 1.5 inches (19, 25 or 38 mm) |
| Layout > Compact, Normal, Loose Padding | The space between paragraphs and lines, and inside table cells |
| Layout > Single, Facing, Opposing Pages | How pages pair up for binding |
| Page > Letter, Legal, A4, A5 | The paper size |
| Page > Gutter Off, Narrow, Wide | Extra margin on the bound edge: none, 0.25 or 0.5 inches (6 or 13 mm) |
| Page > Inches, Millimetres | The units the status line shows |
| Page > Numbers Off, Bottom Center, Header, Outer | Where page numbers go |

With **Facing Pages** the gutter is on the left of odd pages and on the right
of even ones; **Opposing Pages** swaps that. **Single Pages** has no gutter.

The status line at the bottom sums up the setup, for example
`Letter 1in 1pg G.25in Ft 12pt 2006/8190`: paper, margins, pages, gutter,
page numbers (No#, Ft, Hd or Out), body type size, and characters used.

## What markup TeXPad understands

- **The document:** `\documentclass[...]{article}` or `{book}`, with
  `letterpaper`, `legalpaper`, `a4paper`, `a5paper`, `10pt`, `11pt`, `12pt`
  and `twoside`; `\begin{document}` and `\end{document}`.
- **Titles and headings:** `\title`, `\author`, `\date`, `\maketitle`,
  `\chapter`, `\section`, `\subsection`, `\paragraph`.
- **Text:** `\textbf` for bold; `\emph`, `\textit` and `\underline`, which
  are all shown underlined; `\texttt`. `\large`, `\Large`, `\small`,
  `\normalsize`.
- **Blocks:** `center`, `quote`, `abstract`, `itemize` and `enumerate` with
  `\item`, `tabular` with `\hline` and `&` between cells, and `table` with
  `\caption` and `\centering`. `\cite{...}` prints its key in brackets.
- **Spacing and pages:** `\\` or `\newline`, `\par`, `\noindent`,
  `\newpage`, `\vspace`, `\bigskip`, `\medskip`, `\smallskip`,
  `\pagestyle` (`empty`, `plain`, `headings` or `myheadings`), and the
  measures `\parindent`, `\parskip`, `\baselineskip`,
  `\tabcolsep`, `\oddsidemargin`, `\topmargin`, `\textwidth`,
  `\textheight`.
- **Characters:** `\%`, `\&`, `\$`, `\#`, `\_`, `\{`, `\}`, `\textbackslash`,
  `~` for a space that does not break, `%` to start a comment, and `\TeX`,
  `\LaTeX` and `\today`, which prints the date from the system clock, such
  as `October 17, 2026` (also inside `\date{}`).

A blank line starts a new paragraph. A command TeXPad does not know is
skipped along with what it encloses, so a document written for full LaTeX
still opens and reads.

## Files

TeXPad opens and saves `.TEX` source files of up to 8,190 characters. Lines
are saved with Unix line endings; it reads files with DOS or Unix line
endings alike, and Note Pad can open what it saves. Exports are written in
the folder you choose in the file dialog.

## Good to know

- There is no math, no pictures and no colour. Text between `$` signs is set
  as ordinary text.
- Settings written in the source win. If `\documentclass` says
  `letterpaper`, typesetting goes back to Letter even after you pick A4 on
  the Page menu. Change the source to change the paper for good.
- When the document is full, typing stops and TeXPad says **Document full
  (8K)**. A very long document can also be too large to export, and the
  export says **Export too large**.
- There is no Undo.
- **TeXPad > About TeXPad** shows who made it. **TeXPad > Close** closes
  this TeXPad.
