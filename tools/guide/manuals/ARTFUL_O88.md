# ArtfulType

ArtfulType is a writing program that takes the whole screen. There is no
window frame, no dock and no desktop while you write: only your words and a
menu bar. You write in Markdown, the plain-text way of marking headings,
bold, italics and links, and ArtfulType shows the styling as you type. The
file it saves is ordinary text that any computer can read.

It is a port of ActionRetro's ArtfulType for the classic Macintosh.

![Writing in ArtfulType: headings, bold, italic, code and a link, styled as you type.](shot:artful-writer){full}

## Starting ArtfulType

ArtfulType is in the `APPS` folder of the apps disk, and at the top of the
Office disk. Double-click **ARTFUL.O88**. You can also double-click any `.MD`
file to open it straight into the writing screen. The Office disk's `MEDIA`
folder has [[WRITING.MD]], a short guide to ArtfulType that also shows off its
styling.

![The start card.](shot:artful-splash){right}

Started on its own, ArtfulType opens a small window, the start card.

Click **New** (or press `N` or `Enter`) to start an empty document, or
**Open...** (or press `O`) to choose a file. Either one takes the whole
screen. Press `W` to go back to the document you were already writing. The
start card's **File** menu on the menu bar has the same three choices: New,
Open... and Write.

## Leaving the writing screen

There are three ways out, and your document stays as it is in all of them:

- `Esc` returns to the start card. Press `W` there to go back in.
- The small box at the right end of ArtfulType's menu bar sends ArtfulType
  to the dock. Click its tile in the dock to come straight back to your
  document, full screen.
- `Alt-Enter` switches between the start card and the writing screen.

**File > Quit** (`Ctrl-Q`) also returns to the start card, after asking about
unsaved changes.

## Writing

Type, and the text appears at the caret. Lines wrap to the screen. Press
`Enter` to start a new paragraph. `Tab` types four spaces.

| Key | What it does |
|---|---|
| Arrow keys | Move the caret |
| `Home` / `End` | Go to the start or end of the line |
| `Page Up` / `Page Down` | Move a screenful |
| `Shift` with any of these | Select as you move |
| `Backspace` / `Delete` | Delete before or after the caret, or the selection |

Click to put the caret somewhere, and drag to select. The scroll bar on the
right moves through a long document.

## Markdown, as you type it

| You type | You get |
|---|---|
| `#` and a space at the start of a line | A large heading |
| `##` and a space | A smaller heading |
| `###` and a space | A smaller one again |
| two stars, `**`, on each side of some words | **bold** |
| one star, `*`, on each side | *italic* |
| a back-tick on each side | code, on a grey band |
| two tildes, `~~`, on each side | text with a line through it |
| `[words](address)` | the words underlined, with the address hidden |

A style lasts until its closing marks or the end of the paragraph. The
address of a link shows again when the caret steps inside it.

You do not have to type the marks yourself. Select some text and use the
**Style** menu, below.

## Two views

![The View menu, open over the Writer view.](shot:artful-view-menu){right}

The **View** menu has two ways to look at the same file:

- **Writer** hides the marks and shows the styling, with headings scaled
  up. The menu bar is black in Writer view.
- **Markdown** shows every character exactly as it is in the file, at one
  size, on a white menu bar.

![The same file in the Markdown view.](shot:artful-markdown){full}

The view only changes how the text looks. The file is the same either way.

## The File menu

| Item | Key | What it does |
|---|---|---|
| New | `Ctrl-N` | Starts a new, empty document |
| Open... | `Ctrl-O` | Opens a file, using the file dialog |
| Save | `Ctrl-S` | Saves under the current name |
| Save As... | | Saves under a new name, using the file dialog |
| Quit | `Ctrl-Q` | Returns to the start card |

A new document is called `UNTITLED.MD`. Saving it for the first time asks
for a name. If the document has unsaved changes, New, Open and Quit ask
**Save changes to ...?** first, with **Save**, **Cancel** and **Don't Save**.

## The Edit menu

| Item | Key | What it does |
|---|---|---|
| Undo | `Ctrl-Z` | Takes back the last change |
| Redo | `Shift-Ctrl-Z` | Puts back what Undo took away |
| Cut | `Ctrl-X` | Moves the selection to the clipboard |
| Copy | `Ctrl-C` | Copies the selection to the clipboard |
| Paste | `Ctrl-V` | Puts the clipboard's text at the caret |
| Select All | `Ctrl-A` | Selects the whole document |

Undo goes back up to fifteen steps, and a run of typing counts as one step.
If memory is short, Undo and Redo stay grey.
The clipboard is the system's, so you can copy between ArtfulType, [[NOTEPAD.O88]]
and other programs.

## The Style menu

| Item | Key | What it does |
|---|---|---|
| Bold | `Ctrl-B` | Puts `**` around the selection, or takes them off |
| Italic | `Ctrl-I` | The same with `*` |
| Code | `Ctrl-K` | The same with back-ticks |
| Strikethrough | | The same with `~~` |
| Heading 1, 2, 3 | | Makes the caret's line a heading of that level |
| Link | `Ctrl-L` | Makes the selection a link and puts the caret where the address goes |
| None | | Removes the Markdown marks from the selection |

With nothing selected, Bold, Italic, Code and Strikethrough put an empty pair
of marks at the caret, ready to type between. Choosing the heading level a
line already has turns it back into plain text.

## The View and Help menus

| Item | What it does |
|---|---|
| View > Markdown | Shows the file's characters exactly |
| View > Writer | Shows the styling and hides the marks |
| View > Zoom In | Larger text |
| View > Zoom Out | Back to the smaller size |
| View > Default Size | The normal text size |
| Help > About The Artful Type... | Who made it |

There are two text sizes. The zoom lasts until ArtfulType is closed.

## Files

ArtfulType reads and writes plain text Markdown files, usually named `.MD`.
It saves lines with Unix line endings, which is what Markdown tools on
modern computers expect. It reads files with DOS or Unix line endings, and
leaves out characters that are not ordinary letters, digits or punctuation.
A document can be up to 60KB.

When something goes wrong with a file, ArtfulType says what in a sentence,
such as **The disk is full.** or **That file is protected.**

## Good to know

- **New** and **Open...** on the start card ask about unsaved changes over
  the writing screen first. Use `W` to get back to the document you were
  writing without starting another.
- Closing the window with its close box asks **Save changes before
  closing?** if there is unsaved work.
- If the document cannot hold a paste, ArtfulType says **Not enough room in
  the document.**
- Only one program can have the whole screen at a time. If another one has
  it, ArtfulType stays on its start card.
