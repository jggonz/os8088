# Frotz

Frotz plays interactive fiction: text adventures in the story format Infocom
used for Zork, Planetfall and its other games, and that Inform still writes
today. The story describes where you are; you type what you want to do, such
as `open the mailbox` or `go north`, and the story answers. Frotz handles the
status line, saving and restoring your game, sound, colour and pictures.

Frotz is a program of its own written for os8088, named after the Frotz
interpreters on other computers. It plays stories of every version, 1 to 8.

![Frotz before a story is opened.](shot:frotz-window){full}

## Starting Frotz

Frotz is in the `APPS` folder of the first everything disk and of the live
USB stick and CD. Double-click **FROTZ.O88** to open an empty Frotz window,
or double-click a story file to open Frotz with that story already loaded.
You can open more than one Frotz, each with its own story.

The whole story is held in memory while you play, so the machine's memory
decides which stories it can run. A 640KB machine plays nearly every story;
a 256KB machine plays the smaller ones, such as the version 3 Infocom games.

## Getting stories

A story is a single file, usually ending in `.Z3`, `.Z5` or `.Z8` (the
number is the story's version). Infocom's own disks call it `.DAT`. Frotz
opens all four.

The live USB stick and CD carry a library of free stories in the `STORIES`
folder, sorted into `INFOCOM`, `CLASSIC` and `MODERN`, with a `CATALOG.TXT`
that describes each one. Among them are Mini-Zork I, the two Infocom
Samplers, Zork: The Undiscovered Underground, Colossal Cave Adventure,
Curses, Photopia, 9:05, Lost Pig, Bronze and The Dreamhold.

Hundreds more are free on the Interactive Fiction Archive (ifarchive.org).
Copy the story file onto a floppy on any PC; os8088 reads ordinary PC disks.

## Opening a story

![Choosing a story with File > Open Story....](shot:frotz-open){right}

Choose **File > Open Story...**. The Open dialog lists the files on the
disk; find the story and open it. The window takes the story's name and the
game begins.

Frotz checks the story's size before it reads a byte. If the story cannot
fit in this machine's free memory, the window says **Story too large for
this machine** and nothing is loaded. A file that is not a story at all is
refused with **Not a Z-machine story file**. The reason appears under the
empty window's own lines and stays there until you open a story.

A saved game (`.SAV`) opens Frotz too, but a saved game does not say which
story it belongs to, so Frotz says **A saved game: open its story, then
RESTORE.** Open the story, then type `restore`.

![A file that is not a story, refused.](shot:frotz-notstory){full}

## Playing

Type a command and press `Enter`. What you type appears as you type it;
`Backspace` takes back the last letter. Frotz has no command history: each
command is typed fresh.

When a story prints more than fits in the window, the last line reads
**[MORE]** in reverse. Press any key to see the rest.

Most older stories show a status line across the top with the room you are
in and your score and moves, or the time of day. Newer stories draw their
own status lines, boxes and quotations at the top of the window.

The text you have already read scrolls up out of the window and is kept.
Press `Page Up` and `Page Down`, or use the scroll bar on the right, to read
back. Those two keys always scroll the window, even when a story is waiting
for a single key.

You can resize the Frotz window. New text wraps to the new width at once.

## Saving and restoring a game

Save and restore with the story's own commands.

1. Type `save` at the story's prompt and press `Enter`.
2. Frotz says **Press RETURN to choose a file.** Press `Enter`.
3. The Save dialog opens with a name made from the story's, such as
   `MINIZORK.SAV`. Choose a folder, change the name if you like, and save.

Restoring works the same way: type `restore`, press `Enter` twice, and pick
the saved game in the Open dialog. If you cancel either dialog, the story
keeps waiting; press `Enter` to bring the dialog back.

![The File menu.](shot:frotz-file-menu){left}

**File > Save Game...** and **File > Restore Game...** open the same two
dialogs, in place of pressing `Enter` at the prompt. They can be chosen only
while the story is waiting for one: at any other time they are greyed and
read **Save Game (type SAVE)** and **Restore (type RESTORE)**, which is what
to do instead.

Saved games use the standard Quetzal format, so a game saved here can be
carried on with Frotz on another computer, and the other way round.

Many stories also understand `undo` (take back your last move), `restart`
and `quit`. When a story ends, open another with **File > Open Story...** or
close the window.

## The Frotz menu

| Item | What it does |
|---|---|
| About Frotz | Shows what Frotz is and who made it. Press a key or click to put it away |
| Close | Closes this Frotz and the story in it |

![The About card.](shot:frotz-about){full}

## The File menu

| Item | What it does |
|---|---|
| Open Story... | Opens a story file and starts it |
| Save Game... | Opens the Save dialog for a save the story has asked for; greyed otherwise |
| Restore Game... | Opens the Open dialog for a restore the story has asked for; greyed otherwise |

## The Story menu

| Item | What it does |
|---|---|
| Start Transcript / Stop Transcript | Starts or stops a written copy of the game in `SCRIPT.TXT` |

Many stories have a `script` command that keeps a written copy of the game,
which on old computers went to the printer, and `unscript` to stop it.
**Story > Start Transcript** does the same for any story, and the item reads
**Stop Transcript** while a transcript is running, however it was started.
Frotz writes the copy, your commands included, to a text file called
`SCRIPT.TXT` in the folder you last chose a file in, and brings it up to
date each time you press `Enter`. It holds about 16,000 characters; when it
fills, the window says **Transcript full - it stops here.** The item is
greyed before a story is open, and reads **Transcript (NoRam)** when the
machine had no memory left for it.

## Keys

| Key | What it does |
|---|---|
| Letters, numbers and punctuation | Type a command |
| `Enter` | Send the command; also the key that opens the save and restore dialogs |
| `Backspace` | Take back the last letter |
| Any key at **[MORE]** | Show the next screenful |
| `Page Up`, `Page Down` | Read back through earlier text |

## Pictures, sound and colour

- **Colour.** On a colour screen Frotz shows the colours a story asks for.
  On a black-and-white screen it keeps to black on white so the text stays
  readable. Bold text is drawn heavier on a colour screen and in reverse on
  a black-and-white one; italic text is underlined.
- **Sound.** The high and low beeps that Infocom stories use play exactly.
  Other sound effects are played as the nearest beep.
- **Pictures.** Pictures work in version 6 stories that come with a
  picture file in os8088's own `.PIX` format beside them. Infocom's own
  `.MG1` picture files are recognised but not shown; the window says so.

## Good to know

- Stories that use real-time events, where something happens while you are
  not typing, wait for you to type instead.
- When you make the window narrower, text already in the scroll-back keeps
  its old line breaks. New text wraps to the new width.
- If the machine runs short of memory, a story still starts but may tell you
  **No room for undo; play continues** or **No room to save; play
  continues**. Close other programs and open the story again to get them
  back.
- A save from another story is refused with **That save is for another
  story**. Saves written in the compressed form some other interpreters use
  are refused with **Compressed saves cannot be read**.
- `restart` reads the story again from its disk, so leave that disk in the
  drive while you play.
- If a story does something Frotz cannot run, it stops with a line that
  starts **[Frotz:** and ends **the story has stopped.]** Open the story
  again to start over.
