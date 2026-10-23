# Parallel link

`NET.DRV` links this machine to a PC running DOS over a LapLink parallel
cable. A folder on the PC then appears here as an ordinary drive: open it in
a Disk window, copy files to and from it, and open its documents in your
programs. It is the easy way to bring files over from a modern computer by
way of a DOS PC, or to share one PC's hard disk.

## What is inside it

- The link over the cable, through this machine's printer port. It finds
  the port by itself.
- The drive: the PC's files and folders as the PC sees them, so there is no
  size limit and no image file to prepare.
- Network connections over the cable. When the PC is connected to a network,
  Telnet and the Browser can reach it through the PC if this machine has no
  network card of its own.
- The Control Panel's **os88net** page, with the Connect button.

## The other end: OS88NET.COM

The PC runs `OS88NET.COM`, which is on the software disks in the
`SYSTEM/DOS` folder. Copy it to the PC and run it in the folder you want to
share. Its options:

| Option | What it does |
|---|---|
| *folder* | Share this folder and everything under it. Without one, the current folder |
| `/W` | Share every drive on the PC; the link's top level then lists drives |
| `/RO` | Refuse every write, so this machine can only read |
| `/N` | Share the PC's network connection as well. Needs a packet driver and a DHCP server on the PC's network, and takes the network for the length of the run |
| `/P:base` | Use this printer port, such as `/P:378`, instead of searching |

Esc stops it.

## Why it is on the system disk

Finding the cable means writing to the printer ports, and a port may have a
printer on it, so the driver is never loaded unless you ask. It uses about 5K
of memory while loaded.

## Turning it on

Open the **Control Panel**, choose **Drivers**, and tick **os88net**. The
driver looks for a printer port; with none, it says **No hardware found**.
Once loaded, an **os88net** page appears in the list. It shows:

- **Port**: the printer port the driver found.
- **Link**: **No partner** when nothing answers on the cable (or a printer
  does), **Linked** once the PC answers, or **Lost on** and what it was
  doing when the link dropped.
- **Mode**: **Files**, or **Files, read only** when the PC runs with `/RO`.

Start OS88NET on the PC, then click **Connect**. The PC's folder appears on
the desktop as a drive. **Disconnect** takes it away.

## Good to know

- The cable is a LapLink parallel cable, the kind used for copying files
  between PCs.
- Close the Control Panel to load the driver at every start.
- The RAM disk can be loaded at the same time.
- The small system for 128KB machines loads no drivers and leaves it off.
