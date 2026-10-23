# OS88NET

`OS88NET.COM` is an MS-DOS program for a second PC, the one at the other
end of a LapLink parallel cable. Run it there and the os8088 machine sees
that PC's files as an ordinary drive: it can list folders, open documents,
and copy files in both directions. With the `/N` switch it also lends the
DOS PC's network connection to os8088, so the Browser and Telnet can reach
the network through it.

![OS88NET's help, printed by OS88NET /? in os8088's own DOS window.](shot:dos-window)

## What you need

- A PC running MS-DOS, an 8088 or anything newer, with a parallel port.
- A LapLink parallel cable (the kind sold for copying files between PCs)
  between its parallel port and the os8088 machine's.
- On the os8088 machine, the **os88net** driver, `NET.DRV`, switched on in
  the Control Panel.
- For `/N` only: a packet driver for the DOS PC's network card (the kind
  mTCP uses), loaded before OS88NET, and a DHCP server on that network.

## Getting it onto the DOS PC

It is in the `SYSTEM/DOS` folder of the software disks in every size, the
network disk, the second disk of the everything set, the video demo drives
and the live USB image and CD. The floppies are ordinary DOS disks, so copy
it off with the DOS PC's own `COPY` command, for example
`COPY A:\SYSTEM\DOS\OS88NET.COM C:\`. The file unpacks itself in memory each
time it starts, so there is nothing to install.

## Running it

Change to the folder you want to share and type `OS88NET`, or name the
folder. The switches:

| Switch | What it does |
|---|---|
| *folder* | Share this folder and everything under it. Without it, the current directory. os8088 cannot reach anything outside it |
| `/W` | Share every drive on the PC instead. The top of the os8088 drive then lists the PC's drives |
| `/RO` | Refuse every write, so os8088 can read but change nothing |
| `/P:base` | Use this parallel port, in hex: `/P:378`, `/P:278` or `/P:3BC`. Without it, OS88NET tries each port and takes the first that answers |
| `/N` | Share the PC's network connection as well as its files. It takes the network for the whole run, so do not start other mTCP programs beside it |
| `/?` | Print the switches and stop, without touching a disk or a port |

OS88NET then prints what it is doing: the folder it serves, whether writes
are allowed, the port and how it was chosen, and **Listening**. On the
os8088 machine, open the Control Panel's **os88net** page and click
**Connect**: the DOS PC prints **Called on** and the port, and a new drive
appears on the os8088 desktop.

While the link is up, the DOS PC prints one line for every file os8088
touches, with a letter for what was done: `[R]` read, `[W]` write, `[A]`
append, `[D]` delete, `[N]` rename, `[M]` and `[K]` a folder made or
removed, `[S]` a file looked at, and `[Y]`/`[y]` the two ends of a copy.
Listings are not shown. Press `Esc` on the DOS PC to stop.

## Good to know

- The link moves about 3.7KB a second, slower than a floppy, but with no
  disks to carry.
- `/W` has no fence: a delete on os8088 deletes on the DOS PC's real disk.
  Use `/RO` the first time, and for anything you would mind losing.
- OS88NET answers a disk error itself instead of stopping at DOS's Abort,
  Retry, Fail question, since nobody may be sitting at the DOS PC.
- It is not a resident program: it runs until you press `Esc`, and the PC
  is a transfer station while it does.
- It also runs in os8088's own DOS window, where `OS88NET /?` shows its
  help, as in the picture.
