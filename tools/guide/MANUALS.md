# Writing a manual for the project guide

The guide (`tools/os88guide.py`) shows every released disk; clicking a program
opens its manual in the Info window. A manual is written for a PERSON USING
os8088, and should be able to stand on its own as that program's manual.

## Files

| file | what |
|---|---|
| `tools/guide/manuals/<NAME>_<EXT>.md` | the manual, e.g. `NOTEPAD_O88.md` for `NOTEPAD.O88` |
| `tools/guide/capture/<NAME>_<EXT>.py` | its screenshot script (one `capture(ui, shot)` function) |
| `build/guide/shots/<id>.png` | the screenshots it takes. Never committed |

`tools/guide/manuals/CALC_O88.md` and `tools/guide/capture/CALC_O88.py` are the
worked example. Match their tone and shape.

## The shape of a manual

```
# <Program name, as its window or menu bar calls it>

One or two paragraphs: what the program is and what you can do with it.

![Caption saying what the picture shows.](shot:<prefix>-window)

## Starting <Program>
Where it lives (which folder) and how to open it; whether several can be open.
What it needs, if anything (a sound card, VGA, a hard disk, a network card...).

## <The main things you do with it>      one section per real task, in the
## ...                                   order a new user meets them

## The <X> menu                          a table: Item | Key | What it does
## Keys                                  only if there are keys not on a menu

## Files                                 what it opens and saves, and where

## Good to know                          limits, refusals, tips, short list
```

Link to another file's page or manual with `[[NAME.EXT]]` (for example `[[CHART.O88]]` or `[[CHARTER.F88]]`); clicking it opens that file's Info. Use it where a reader would want to go next, not on every mention.

Supported Markdown: `#`/`##`/`###` headings, paragraphs, `-` and `1.` lists,
`| tables |`, `> note`, `**bold**`, `*italic*`, `` `code` ``, links,
`![caption](shot:id)` on a line of its own, and `![caption](video:cga/03-cga4)`
for a demo video the reader can play (the names are the files in
`tools/guide/videos/`). Shots or videos on lines with no blank line between
them make a gallery, side by side. Nothing else (no HTML, no nested lists, no code blocks).

## Rules for the words

- **User-facing only.** Never mention SPEC.md, section numbers (§), `OSAPI_*`,
  kernel internals, the assembler, cycle counts, memory layouts, or how the
  program is built. Say what the user sees and does.
- **Every fact comes from the source.** Menu items, keys, limits and messages
  are in the package's source (`apps/<dir>/`, its strings and key tables) and in
  its SPEC.md section. Read those; do not guess and do not describe a feature
  the program does not have. Use the exact menu and item names the program
  shows.
- Plain, direct sentences. Active voice. No em-dashes, no "not X but Y", no
  marketing. Short paragraphs.
- Name other programs by their window names (Note Pad, TeXPad, Paint...).
- Length follows the program: a small game is ~300 words, Paint or Sheet can
  be 1,500+. Cover every menu item and every key; skip nothing a user can
  touch.

## Screenshots

    python3 tools/guide/capture/run.py <NAME>_<EXT> [--apps build/<disk>.img] [--machine os8088_xt_vga]

boots the 360KB system disk with `build/apps360.img` in B: on a VGA XT under
MartyPC (one boot, about a minute), runs your `capture(ui, shot)`, and writes
the PNGs. `ui` is `tools/os88ui.py`'s UI: `ui.path("B:/APPS/NOTEPAD.O88")`
opens a program, `ui.menu_pick("File", "Open...")` picks a menu item,
`ui.m.type_text("hello")` and `ui.m.key("Enter")` type, `ui.window("Note Pad")`
finds a window, `ui.mo` is the mouse (`tools/os88mouse.py`). Read
docs/MARTYPC-DEBUG.md's "Start at tools/os88ui.py" if you need more.
`shot(id)` takes the whole screen (all 480 rows of a 320x240 game, which MartyPC's first display area would cut to 400); `shot(id, window="Title")` crops to that
window; `shot(id, rect=(x, y, w, h))` to a rectangle.

- **Placement.** `![caption](shot:id)` on its own line lets the page decide:
  the first shot takes the full width, and after it a shot up to 320 pixels wide floats to the right with the text wrapping
  around it, a wider one takes the full width. Add `{left}`, `{right}` or
  `{full}` straight after it to choose: a small dialog or a menu reads best
  beside the paragraph about it, a whole window or a full-screen game at full
  width. Alternate sides when two floats come close together, and start a
  new section rather than stack floats. Every shot can be clicked to enlarge.
- Prefix every shot id with the program, e.g. `notepad-window`,
  `notepad-find`. Ids are lowercase letters, digits and dashes.
- **3 to 8 shots per program**, each showing something the text describes.
  The first is the program open and doing something real (not an empty
  window), placed right after the opening paragraphs.
- Programs on other disks: pass `--apps build/word360.img` and so on (the
  disk list is in `build/`; files are at the root or in a folder - check with
  `python3 tools/os88fat.py ls build/<disk>.img`).
- Fullscreen programs and games: a whole-screen `shot(id)` once the game is
  running. If a program cannot be reached on this machine (it needs a network
  card, a 286, a sound card MartyPC lacks), use a screenshot of what CAN be
  shown and say in the text what it needs.
- LOOK at every PNG you take (open it) before you put it in the manual. A
  shot that shows the wrong thing is worse than none.

## When a capture will not boot

`run.py` reads the kernel's symbols from `build/kernel.bin`. If that and the
images in `build/` ever disagree, every boot fails with "...reach the desktop
did not happen". Do NOT run `make` in `build/` (other agents are using it).
Build a private tree and point the run at it:

    python3 -c "import sys; sys.path.insert(0,'tools'); import os88build; print(os88build.tree(targets=('os8088-360.img','apps360.img')).apply())"
    OS88_BUILD=<that tree> python3 tools/guide/capture/run.py <KEY> --image <that tree>/os8088-360.img --apps <that tree>/apps360.img

and say so in your report.

## Where things are

- `build/apps360.img` is the 360KB software disk: APPS (most programs),
  GAMES, and only three documents in MEDIA. The other sample files (pictures,
  SALES.SLK, WRITING.MD, songs) are on `build/office360.img`,
  `build/media360.img` or the 1.44MB `build/apps.img`; use whichever disk
  has what your shot needs. The system disk has APPS (core programs),
  STIKAPPS (Desk Calendar, Sticky Note, Desk Clock), SYSTEM (The Wire, Task Manager, FONTS).
- A sound card: `--machine os8088_xt_vga_sb` is the VGA XT with a Sound
  Blaster (`tools/martypc/configs/os8088_machines.toml` lists every machine).
- MartyPC has no network card. Show what a program does without one, and say
  in the text what it needs.

## Budget

These are pictures for a manual, not tests. One to three capture runs per
program, no soak rows, no `make test-*`, no rebuilding the kernel or disks.
Do not edit shared files (`entries.toml`, `page.html`, `os88guide.py`,
`capture/run.py`, other programs' manuals). Do not commit. If a description
in `tools/guide/entries.toml` is wrong, say so in your report instead.

# File pages: drivers, system files, typefaces, folders and data

Every other file on a disk gets a page too, in the same place and the same
way: `tools/guide/manuals/<NAME>_<EXT>.md` (a folder is its name alone:
`FONTS.md` for `FONTS/`), with an optional capture script beside the others.
They are shorter than a program's manual and answer four questions:

1. **What it is and what it does for you**, in a sentence or two.
2. **What is inside it**, in user terms: "the Control Panel's pages, loaded
   when you open it and freed when you close it", "ten songs by...", "the
   typeface Charter in sizes...". No offsets, formats or internals beyond
   what a curious user would want.
3. **Why it is on this disk**, and what happens if it is missing or which
   disks carry it (some names are on several disks with DIFFERENT contents,
   such as README.TXT: say what each one is).
4. **Using it**, from the user's side: where it shows up (a Control Panel
   page, a menu, a double-click that opens it in a program), how to turn it
   on or off, what it needs. A screenshot of that is welcome where there is
   something to see (the Control Panel page that switches a driver on, a
   song open in its player, a picture open in Paint or PiXEL, a typeface in
   Font Viewer). Skip screenshots where there is nothing to see.

**Plain-text files show their own words.** For `.TXT`, `.ME`, `.MD`,
LICENSE and COPYING the guide reads the file off each disk and prints it
under your page, per disk. So a text file's page is a short description
(what it is, who it is for, which disks carry which version) and never a
copy of the text.

Shape: `# <title>` (the title from entries.toml), a short opening, then `##`
sections as needed. 100 to 400 words; a driver with real settings may run
longer. A song, picture or sample document can be a few lines plus one shot.
Do NOT write pages for programs (`.O88`), which have manuals, or for CP/M
files and demo videos, whose descriptions come from where they are.
Shot ids are prefixed with the file, e.g. `sound-drv-page`, `charter-f88`.

# Keeping pages true: the guide lock

`tools/guide/manuals.lock.json` pairs every page with the sources it
describes and a fingerprint of the text a user reads in them (menu items,
messages, labels, a tool's options). `python3 tools/guidelock.py check`
reports a page whose sources changed that text since it was stamped, and a
file on a built disk with no line in entries.toml or, for a program, driver
or system file, no page. The fast tier (every `make`) only warns; the full
tier fails until it is paid up.

**A manual is the current state, never a history.** When a page is flagged,
ask what a first-time writer, reading today's program, would have written,
at the page's current level of detail. Change only that. Most flagged
changes need no words: an internal figure, a message for a rare case, a fix
or a reworded label the page never quoted. Stamp those unchanged with the
reason. Never write "now", "new", "no longer", "used to" or "since": the
reader of a manual does not care what the program was, and a page that
grows a sentence with every commit stops being a manual and becomes a
changelog.

To pay it: read the page against the change, correct it if it needs it, then
`python3 tools/guidelock.py stamp <KEY> --why "what you checked"`. A new
page is stamped the same way; add `--sources apps/x,drivers/y` when the
guess is wrong (an alias table in guidelock.py covers the known cases).
