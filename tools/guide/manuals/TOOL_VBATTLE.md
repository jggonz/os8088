# Video Battle builder

The Video Battle builder runs on your modern computer, not on os8088. It
makes a battle that Video Battle ([[VBATTLE.O88]]) plays: a `.VBT` file built
out of your own footage. You give it an idle loop, the clips of each action,
the music, the menus, the characters and the enemy, and it joins every clip
to the loop exactly, checks every join, fits the whole battle to the machine
it is made for, and puts it on a floppy or a hard disk image with Video
Battle beside it.

There are two ways to use it. **os88vbtgui.py** opens the builder's window,
with a tab for each part of the battle and previews of how it will look.
**os88vbt.py** does the same from the command line, and is what the window
runs to build.

## What it needs

- **Python 3.11 or newer.** The window also needs Tk. The python.org
  installer includes it; on Linux install the `python3-tk` package.
- **Two Python packages**, numpy and Pillow:
  `python3 -m pip install numpy pillow`
- **ffmpeg and ffprobe** on your PATH, from ffmpeg.org. They read your
  footage and your sounds.
- Optional: the `tkinterdnd2` package, so you can drop files on the window
  on macOS and Linux. On Windows a drop works without it, and every drop has
  a button that does the same.
- Optional, to listen in the window: `afplay` on macOS, nothing extra on
  Windows, or `paplay`, `aplay` or `ffplay` on Linux.

## Getting the builder

In an os8088 source tree the programs are in `tools/`. Start the window with
`python3 tools/os88vbtgui.py`, or `python3 tools/os88vbtgui.py PROJECT.vbp`
to open a project.

Without the source tree, use the builder's folder, `BATTLE`, in the tools
zip. It holds the window, everything it needs, Video Battle (`VBATTLE.O88`)
and its play-record version (`VBATTLED.O88`), the files a bootable hard disk
needs, a `README.TXT` and `GUIDE.HTML`: a full guide to open in a web
browser, which walks through making a battle tab by tab and says when and
why to reach for each setting. Run `python3 os88vbtgui.py` inside that
folder.

## The window

A battle is a PROJECT, a `.vbp` file. The tabs follow the order you build
it in:

| Tab | What it is for |
|---|---|
| Battle | The battle's name, its title picture, the screens it is made for, and the footage and sounds it uses |
| Idle | The loop the battle stands on between actions, and where it goes round |
| Clips | Each action's clip: where it starts and ends, and where it joins the loop |
| Openings | What plays before the first turn |
| Title & endings | A title that goes round until a key, and how victory and defeat end |
| Actions | What each command does: its effects, its cost, its target and its clips |
| Characters & menus | The party, each character's menus, and the band's layout |
| Enemy | The enemy, its stances and its moves |
| Sounds | Every sound and its level |
| Build | Build the `.VBT`, see what is wrong, what it costs the machine, and make a disk or a game |

A project keeps the frames it has encoded in a `PROJECT.cache` folder beside
it. The first look at a video takes a minute or two; every one after reads it
back.

## What it makes

- **BATTLE.VBT** for CGA, and, when the Battle tab asks for them,
  **BATTLEV.VBT** for VGA and **BATTLEH.VBT** for Hercules, beside the
  project. Put the one for your screen beside VBATTLE.O88, or all three.
- **A disk** with the battle and Video Battle on it: a floppy, or a hard disk
  image for an emulator or a real disk, which boots os8088.
- **A game**: one `.O88` file for each screen, which carries its battle inside it,
  so the battle plays when you double-click it.

## Using the command line

Everything the window does is also a command. In the builder's folder:

| Command | What it does |
|---|---|
| `python3 os88vbt.py demo-project demo.vbp` | The demo battle as a project, made with no footage, to look at or start from |
| `python3 os88vbt.py build PROJECT.vbp` | Build the battle: `BATTLE.VBT`, and the VGA and Hercules versions the project asks for, beside it |
| `python3 os88vbt.py disk PROJECT.vbp --disk 3` | Build it and make a disk of it: `--disk` 0 to 3 are the floppies, 4 to 6 the hard disks |
| `python3 os88vbt.py recommend PROJECT.vbp` | The idle loop's and every clip's best points, with their scores |
| `python3 os88vbt.py verify BATTLE.VBT` | Play every path of a built battle on the computer and check every join |
| `python3 vbtproj.py PROJECT.vbp` | List what is wrong with a project |
