# Sound driver

`SOUND.DRV` lets programs play music and sound on a sound card. Without it
os8088 still beeps and plays simple tones on the PC speaker that every
machine has. With it, programs can use FM music, digital sound and MIDI.

![The Control Panel's Drivers page. Sound and Hard Drive are loaded; Ethernet and Ram Disk are not.](shot:sound-drv-drivers)

## What is inside it

The driver knows these kinds of sound hardware:

- **AdLib**, and the FM music chip that every Sound Blaster carries. FM music
  costs the machine almost nothing while it plays, and system beeps move to
  the FM chip too.
- **Sound Blaster**: digital sound such as WAV files and the voices of a
  module, and recording from the card's input.
- **MPU-401**, a MIDI interface. Notes go out to a synthesizer on the
  interface, such as a Roland module or a Waveblaster daughterboard on the
  card, which plays them with its own instruments.
- **Covox Speech Thing**, a small sound plug on a printer port.
- **PicoMEM**, a card that can act as an AdLib and a Sound Blaster. The
  driver switches those sounds on for you, which on DOS needs a separate
  program.

Programs that use it include the Audio Player, Tracker, MIDIRack and the
Video Player, and several games play their music through it.

## Why it is on the system disk

Most machines of the period had no sound card, so the driver is a file on
the system disk and not part of the system itself. It costs memory only on a
machine that loads it: about 24K while a program plays digital sound, and
less while only FM music is in use.

It is not loaded unless it is wanted. The first time os8088 starts on a
machine, it checks quickly for an FM chip. If it finds one, the driver loads
by itself; if not, nothing is read from the disk.

## Turning it on

Click the menu at the left end of the menu bar, choose **Control Panel**,
and pick **Drivers** in its list. Each row is one driver: a box that is
ticked while the driver is loaded, its name, roughly how much memory it uses, and a line
saying what happened, such as **Loaded**, **Not loaded** or **No hardware
found**.

Click the box beside **Sound** to load the driver at once, and click it
again to unload it. Close the Control Panel to keep the choice for the next
start.

## The Sound page

![The Sound page on a machine with a Sound Blaster.](shot:sound-drv-page){right}

The Control Panel's **Sound** page chooses which hardware plays the sound:

| Choice | What it does |
|---|---|
| **PC Speaker** | Everything goes to the built-in speaker, even with a card loaded |
| **AdLib** | FM music only. On a Sound Blaster this gives back the memory digital sound uses |
| **Sound Blaster** | FM music and digital sound |
| **Covox** | Digital sound through a Covox on the printer port named beside it (**LPT1**, **LPT2** or **LPT3**), with FM music still on an AdLib if there is one |

A choice the machine does not have is greyed. **Test** plays one second of a
tone through whatever is chosen. If a choice cannot be used, the line under
**Test** says why: **No hardware found** or **Not enough memory**.

## Where the MIDI interface is

![The Drivers page's Ports view.](shot:ether-drv-ports){left}

The **Ports** button on the Drivers page shows where the driver looks for a
MIDI interface. **Auto** tries the usual addresses, 330h and then 300h. Pick
an address with the arrows if your interface is elsewhere, or **Off** to
leave it alone. The choice applies the next time the machine starts.

## Without a sound card

If the driver finds nothing, its box stays clear with **No hardware found**
under it, and sound goes to the PC speaker as before. If the machine will
never have a card, click the box once more: the line changes to **Not
loaded** and os8088 stops trying at every start.

## Good to know

- Every os8088 system disk carries it, as a hidden system file in the root.
  The small system for 128KB machines loads no drivers and leaves it off.
- A Covox cannot be detected, so it is used only when you choose it on the
  Sound page.
