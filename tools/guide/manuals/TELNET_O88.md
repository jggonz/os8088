# Telnet

Telnet is a terminal for logging in to other computers over the network: a
bulletin board system (BBS), a MUD, or a Unix machine. It shows the other
computer's screen as 80 columns by 25 rows of text in sixteen colours, draws
the menus and pictures that boards make out of ANSI characters, and can
receive files from a board by Zmodem.

![Telnet with an address typed into the host box, ready to connect.](shot:telnet-window){full}

## Starting Telnet

Telnet is **TELNET.O88** in the `APPS` folder. Double-click it to open it.

Telnet needs a network connection. Load the **Ethernet** driver ([[ETHER.DRV]]) for an
NE1000 or NE2000 network card: open the **Control Panel**, choose the
**Drivers** page and tick **Ethernet**. The machine gets its address by
itself from your router, or you can set one by hand on the Control Panel's
Ethernet page with **Set Up**. The Browser's manual, [[BROWSER.O88]], explains these
steps in full. A DOS PC joined to this machine by the **Parallel link** ([[NET.DRV]]) can carry the
connection instead, if that PC is on a network.

## Connecting

The box at the top of the window has the keyboard when Telnet opens: type
the name or address of the computer to reach. Add a colon and a port number if it is not the usual
Telnet port, 23: for example `bbs.example.com:2323`. Press `Return` or click
**Connect**.

The line at the bottom of the window says how it is going: **Looking up**,
**Connecting**, then **Connected**. Once you are connected, everything you
type goes to the other computer, and the button says **Close**. Click it, or
choose **Session > Connect / Close**, to hang up. When the other computer
ends the session the line says **Disconnected**, and **Connect** starts a new
one.

![With no network driver loaded, the connection fails and the bottom line says why.](shot:telnet-failed){full}

When a connection cannot be made, the bottom line gives the reason:

| Message | What it means |
|---|---|
| No link driver | No network driver is loaded |
| No partner | The parallel link is loaded, but the PC at the other end cannot carry connections |
| Refused | The computer answered, but nothing is listening on that port |
| Host closed | The other computer ended the session |
| Link lost | The network went away during the session |

## The screen

The window shows the whole 80 by 25 screen when it opens. Drag the grow box
in the corner to make the window smaller; you then see part of the screen.
The other computer still sees a full 80 by 25 terminal, so nothing it sends
is rearranged.

**Session > Full Screen** (`Ctrl-]`) fills the monitor with the terminal in
real text mode, which is much faster to draw. The bottom right corner says
`^] to leave` until the board draws over it. Press `Ctrl-]` again to go back
to the window. The session carries on in both.

## Keys

Letters, numbers and punctuation go to the other computer as you type them.
These keys send what boards expect:

| Key | What it does |
|---|---|
| Arrow keys | Move, in board menus and games |
| `Home`, `End`, `PgUp`, `PgDn`, `Ins`, `Del` | The board's own meanings for these keys |
| `F1` to `F12` | Function keys, for programs that use them |
| `Return` | End a line |
| `Backspace`, `Esc`, `Tab` | Sent as they are |
| `Ctrl-]` | Switch between the window and full screen. Never sent |

If your machine has no mouse, os8088 uses the arrow keys, `Home`, `End`,
`PgUp`, `PgDn`, `Ins`, `Del` and `Space` to move the pointer. Turn on
**Scroll Lock** to give those keys to the board instead. The About card says
so too.

![The About card, with the Scroll Lock tip.](shot:telnet-about){full}

## Receiving files

When a board starts sending a file by Zmodem, Telnet notices and opens the
standard Save dialog, with the file's name filled in. Choose a disk and
folder and click **Save**. Long names are shortened to fit the disk's 8.3
names. A file with the same name is replaced without asking. Click
**Cancel** to skip that file.

While the file arrives, the window shows its name and how many bytes have
come so far, such as `MYFILE.ZIP  12,345 / 98,765`. The board's screen comes
back when the transfer ends.

If a board does not start the transfer by itself, tell it to send, then
choose **Session > Receive File (Zmodem)...**. The item is greyed when you
are not connected.

If the file cannot be written, the bottom line says why: **Folder full**,
**Disk full**, **Disk is write-protected**, **That name is protected** or
**Disk error**. Telnet then tells the board to stop sending. **Sender
stopped** means the board went quiet during the transfer.

## The Session menu

| Item | Key | What it does |
|---|---|---|
| Connect / Close | | Connect to the host in the box, or hang up |
| Clear Screen | | Clear the terminal screen |
| iCE Colours | | Show bright backgrounds instead of blinking text |
| Full Screen | `Ctrl-]` | Switch between the window and full screen |
| Receive File (Zmodem)... | | Start receiving a file the board is sending |

![The Session menu.](shot:telnet-menu-session){left}

Some boards draw with sixteen background colours and expect a terminal set
to **iCE colours**; others use blinking text. Choose **iCE Colours** to
switch to bright backgrounds; the item then reads **iCE Colours (on)**, and
choosing it again switches back. On a Hercules screen there are no
background colours, so the item reads **iCE Colours (Mono)** and is greyed.

**Telnet > About Telnet** shows who made it; any key or click puts the
terminal back. **Telnet > Close** closes the window and ends the session.

## Good to know

- Telnet can receive files by Zmodem but cannot send them.
- It tells the board it is an ANSI terminal of 80 by 25.
- `F11` and `F12` may do nothing on an XT, whose BIOS does not pass them
  on.
