# Font Viewer

Font Viewer shows the typefaces installed on your system disk. The list on
the left names every face in the `SYSTEM/FONTS` folder; the box on the right
shows a sample line in the face you pick. The sample is yours to change:
type into it and see your own words in each face.

![Font Viewer showing Charter, one of the installed faces.](shot:fontview-window){full}

## Starting Font Viewer

Font Viewer is in the `APPS` folder of the system disk. The Office disk
carries a copy at its top level too. Double-click **FONTVIEW.O88** to open
it on the first face in the list.

![The FONTS folder on the system disk. Double-click a face to see it.](shot:fontview-folder){right}

The quicker way is from the faces themselves. Open the system disk, then
`SYSTEM`, then `FONTS`, and double-click any `.F88` file, such as [[CHARTER.F88]]. Font Viewer opens
with that face already chosen.

You can open more than one Font Viewer at a time.

## Choosing a face

The **FONTS** list shows each family by name, and a `>` marks the one on
show. To choose another:

- click its name, or
- press **Down** or **Right** for the next face, and **Up** or **Left** for
  the one before. The list wraps round from the bottom to the top.

The face's name appears above the sample. While Font Viewer reads the face
from the disk it says `Loading...` after the name, and the sample redraws in
the new face when it has finished.

## Typing a sample

The sample starts as a line with capitals, small letters, figures and
punctuation in it, so a face shows most of its characters straight away.

- Type any letter, figure or symbol to add it to the end of the sample.
- **Backspace** deletes the last character.

The sample holds up to 127 characters. A long sample wraps onto more lines,
and lines that do not fit in the box are not shown. The sample stays the same
when you change faces, so you can compare faces on the same words.

![A sample typed into the Times face.](shot:fontview-typed){full}

## Menus

Font Viewer has no menus of its own. The **Font Viewer** menu in the bar at
the top of the screen has:

| Item | What it does |
|---|---|
| About Font Viewer... | shows who made it. Any key or click closes the card |
| Close | closes this Font Viewer |

## Files

Font Viewer reads the typeface files (`.F88`) in `SYSTEM/FONTS` on the disk
the machine started from, and nothing else. It never changes them, and it
does not save the sample.

The same faces are the ones [[WORD.O88]] and the other programs with a font choice
offer, so Font Viewer is a quick way to pick one before you use it there.

## Good to know

- Font Viewer reads the list of faces when it starts. A face copied into
  `SYSTEM/FONTS` afterwards appears the next time you open Font Viewer.
- Double-clicking a `.F88` file kept in some other folder opens Font Viewer,
  but it shows only the installed faces, starting at the first.
- If the system disk has no `FONTS` folder, the list says `No .F88 files`
  and the sample appears in the machine's own built-in letters.
- If a face cannot be read, its name is followed by `Open failed` and the
  sample appears in the built-in letters. Choose another face and then this
  one again to try once more.
- Type at a steady pace. Holding a key down, or sending many keys at once,
  can lose keystrokes while the sample redraws.
