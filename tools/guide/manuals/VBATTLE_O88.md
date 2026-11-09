# Video Battle

Video Battle plays a turn-based battle whose picture is video. A loop of the
scene plays across the top of the screen while you choose each character's
command from a menu in the band under it. The command's clip joins the loop,
plays out with its own sounds, and joins back onto the loop, so the battle
reads as one continuous scene over its own music.

![The demo battle on a VGA: the BEETLE has appeared, and ARI's menu is open.](shot:vbattle-menu){full}

A battle is a `.VBT` file. Every disk Video Battle comes on carries the demo
battle, drawn and scored for this project: ARI and BRAM against the BEETLE.
Battles of your own are made on a modern computer with the Video Battle
builder, which turns your own footage into a `.VBT` file and a disk to play
it from. Its page in this guide is **Video Battle builder**, under Tools.

## Starting Video Battle

Double-click a `.VBT` file and Video Battle opens with it. You can also
double-click **VBATTLE.O88**: it looks for **BATTLE.VBT** in the same folder.

![The Video Battle window, ready to play.](shot:vbattle-window){right}

The window says what it will do. Press `Enter` or `Space`, or click in it, to
play. The screen goes to the battle until you leave it, and then the window
comes back. While the battle is read the window says **Loading...**, and
**Making room...** when the system first moves memory to give it more.

A battle may show a title picture in the window instead, with a line under
it, usually **Click or press Enter to begin**.

With no battle beside it, the window says **No battle loaded**. Choose
**File > Open...** to pick one, or put `BATTLE.VBT` beside the program.

## Which file it plays

A battle can come in three versions, one for each kind of screen. They sit
side by side and share a name: `BATTLE.VBT` for CGA, `BATTLEV.VBT` for VGA
and `BATTLEH.VBT` for Hercules. Video Battle plays the one for your screen,
whichever of the three you double-click.

| Your screen | It plays |
|---|---|
| VGA | the VGA version, or the CGA version when there is none |
| EGA or CGA | the CGA version |
| Hercules | the Hercules version. A CGA battle cannot play there |

After a play the window's last line names the file it played.

## The title and the opening

![The demo's title, going round until you press Enter.](shot:vbattle-title){left}

A battle may begin with a title that goes round until you press `Enter` or
`Space`. Some battles take any key. The title then fades or leads into the
opening, and the enemy appears. The demo's title says **VIDEO BATTLE**.

## Playing a turn

The characters take their turns in order, then the enemy. The band under the
picture holds three things:

- **The menu** of the character whose turn it is. An action costs MP, an
  item or a full limit gauge, and its cost is shown beside it. One you cannot
  pay for is drawn in red, or greyed on a VGA.
- **The party**: the enemy's name, then each character's name, HP and MP. A
  `*` beside a name means that character's limit gauge is full. Whose turn it
  is is drawn inverse, and a fallen character in red, or greyed on a VGA.
- **The message line** at the bottom, which says what just happened, such as
  **BEETLE appears!** or **Bolt: 180**.

![ARI's Bolt playing. The band waits while the clip plays.](shot:vbattle-clip)

Choose an action and its clip plays. The numbers change when the clip is
over, and the next character's turn begins. The clip always agrees with the
numbers: a missed attack plays a miss, a finishing blow plays the finish.
The enemy then takes its turn with a clip of its own.

![BRAM's turn after ARI's Bolt hit for 180.](shot:vbattle-turn){right}

An action aimed at one of your own party, such as a heal, asks **Who?** first.
Pick the character with `Up` and `Down`, then press `Enter`. An action you
cannot pay for says **Cannot now** and waits for another choice.

The battle ends when the enemy falls or the whole party does. What happens
then is the battle's own: its last picture held, an end scene that goes round,
or a fade back to the title to play again. An escape ends it too, with
**Escaped!   Esc to leave**.

## Keys

| Key | What it does |
|---|---|
| `Up`, `Down` | Move through the menu, or between characters when an action asks **Who?** |
| `Enter` or `Space` | Take the action, or open the sub-menu it leads to |
| `Left` or `Right` | Open the character's side menu, when it has one |
| `Esc` | Go back out of a sub-menu, a side menu or **Who?**. At a top menu, or while a clip plays, leave the battle |
| `Enter` or `Space` at the title | Leave the title and start the battle |
| `Enter` or `Space` at an end scene | Play the battle again |

When you leave, the window says **Enter or click to play again**. Playing
again starts from the beginning, and what was already read is kept, so it
starts sooner.

## The File menu

![The File menu.](shot:vbattle-file-menu){left}

| Item | What it does |
|---|---|
| Open... | Pick a `.VBT` file to play in this window. It replaces the battle the window had |
| Close | Close the window |

## The Video Battle menu

| Item | What it does |
|---|---|
| About Video Battle | Shows who made it |
| Close | Closes the window |

The menu bar is the window's, so it is there between plays, not during one.

## What it needs

- **A CGA, EGA, VGA or Hercules card.** Each plays the version of the battle
  made for it, as above.
- **The full system disk.** On the 128KB system disk the window says
  **Needs the full kernel**.
- **Memory.** A battle reads ahead into the memory it can find, and a play
  needs roughly 200KB free beside the rest of the desktop. When there is not
  that much, the window says **Not enough memory**: close other programs and
  play again.
- **A Sound Blaster for sound**, with the sound driver ([[SOUND.DRV]]) loaded.
  With no card the battle plays silent. It never uses the PC speaker.
- **A hard disk plays a battle best.** A battle reads its music and its clips
  from the disk while it plays, and a hard disk keeps up where a floppy may
  not. The demo on the 720KB and 360KB disks is made for a floppy: its music
  is short enough to be held in memory, and it leaves out its largest clip.
  Copy `VBATTLE.O88` and the battle to a hard disk to play it from there.
- **Extended or expanded memory helps.** On a 286 or later with extended
  memory, or on a machine with an EMS board, the battle is copied into that
  memory while the title waits, and then plays from there, so a clip starts
  without waiting for the disk.

## Files

Video Battle opens `.VBT` files, and saves nothing. A battle can also come
as a program of its own, made by the builder, which plays its battle when you
double-click it.

The VGA disk also carries **VBATTLED.O88** ([[VBATTLED.O88]]), the same
program that writes a record of each play for finding out why a battle
stutters.

## Good to know

- If the window names a file and says **not found**, the battle is not in
  the program's folder. **is not a VBT1 file** means the file is not a
  battle at all. **is an older build: rebuild it** means the battle
  was made by an older builder: build it again. **is damaged** or **a read
  failed** means the file or the disk could not be read whole.
- On a Hercules with only a CGA battle beside it, the window says
  **Needs BATTLEH.VBT on a Hercules**.
- The picture and the sound stay together. When the disk falls behind, the
  picture waits for it rather than running on without its sound.
