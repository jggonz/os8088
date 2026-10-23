# FTP Server

The FTP Server turns this machine into a file server on your network. A
modern computer connects to it with any FTP program, such as FileZilla,
WinSCP or the `ftp` command, and can list, fetch, send, rename and delete
files on this machine's disks while os8088 keeps running. It is the easy way
to move files between a modern computer and an old PC.

The window is a log: it shows each command the client sends and what
happened, and the line under the toolbar says what the server is doing.

![The FTP Server, stopped and ready to start.](shot:ftpd-window){full}

## Starting the FTP Server

The FTP Server is **FTPD.O88** in the `APPS` folder (on the network disk it
is at the top of the disk). Double-click it to open the window. The server
does nothing until you press **Start**.

It needs a network card. Load the **Ethernet** driver, [[ETHER.DRV]], for an NE1000 or
NE2000 card: open the **Control Panel**, choose the **Drivers** page and tick
**Ethernet**. The machine gets its address by itself from your router, or you
can set one by hand on the Control Panel's Ethernet page with **Set Up**. The
[[BROWSER.O88]] manual explains these steps in full. A DOS PC joined by the
**Parallel link** ([[NET.DRV]]) can carry the connection instead, if that PC is on a
network.

## Serving files

Press **Start**. The line under the toolbar changes to something like
**Listening port 21 at 192.168.1.60**: that address is the one to give your
FTP program. Connect to it on port 21. When a client is logged in the line
says **Client connected**. Press the button again to stop the server.

If the server cannot start, the line says why:

| Message | What to do |
|---|---|
| No network driver | Load the Ethernet driver in the Control Panel |
| No network | A driver is loaded but cannot carry connections yet |
| Port 21 is in use | Another FTP Server window is already running |
| No such Root folder | Fix the Root setting (see below) |
| No free task - close an app and retry | Close another program, then press Start again |

![With no network driver loaded, Start stops at once and says why.](shot:ftpd-nodriver){right}

Start is never greyed: fix what the line said and press it again.

**What the client sees.** With no settings, the server shares the folder it
was started from, and everything below it. Started from the `APPS` folder of
a floppy, it shares that folder; started from the top of a disk, it shares
the whole disk. The client can use folders, upload and download, delete,
rename, and make and remove folders.

**Read Only.** Tick **Read Only** on the toolbar to let clients fetch files
but not change anything. A client that tries is told **550 The server is
read only**.

## Settings

Click **Setup**, or choose **Server > Setup...**, to open the settings page.
Click **Done** to go back to the log. The settings are saved when you leave
the page or close the window. An empty box means the default.

| Setting | What it does |
|---|---|
| PASV addr | The address to give clients for passive transfers, when they reach this machine through a router (see below). Empty: the machine's own address |
| Root | The folder to share, such as `A:/DOCS`, `A:\DOCS` or `/DOCS`. Empty: the folder the server was started from |
| User | A user name clients must log in with. Empty: any name and any password are accepted |
| Password | The password that goes with User |
| Read Only | The same box as on the toolbar |
| Serve every drive (ignores Root) | Share every drive: the client sees `A`, `B`, `C` and so on as folders at the top |

![The Setup page.](shot:ftpd-setup){left}

The user name may be typed in any case; the password must match exactly. The
password is kept in plain text in the server's settings file, so treat it as
a lock against casual visitors only. What really decides who can reach your
files is the **Start** button, pressed by someone at this machine.

## Through a router

A client on the same network as this machine needs nothing set. If clients
come in from outside through a router or port forwarding:

- Forward port **21** and the passive data ports **2048 to 2055** to this
  machine. The log reminds you of the range at every Start, and shows each
  data port as it is used.
- Put the address the client dials into **PASV addr**.
- Or use active mode, or a client that uses EPSV: neither needs PASV addr.

## The Server menu

| Item | What it does |
|---|---|
| Start / Stop | Start or stop the server, like the button |
| Clear Log | Empty the log |
| Setup... | Open the settings page, like the Setup button |
| Sockets | Write a list of the network connections into the log, for finding a fault |

![The Server menu.](shot:ftpd-menu-server){right}

**Ftpd > About Ftpd** shows who made it. **Ftpd > Close** stops the server
and closes the window.

## The log

The log shows each command the client sends and the result, newest at the
bottom. A password is shown as the word `PASS` alone. After each transfer it
writes a line such as `Got 129430 in 18s 7190B/s` for an upload or `Sent`
for a download, with the size, time and speed.

## Good to know

- The server takes one client at a time. A second connection is turned away
  with **421 Busy**, and the log says **Refused a second client (421)**.
  Programs that open a second connection while the first is busy, FileZilla
  among them, get that answer and try again. If the first client has said
  nothing for 15 seconds and is not transferring, the newcomer takes its
  place, and the log says **Took over from a silent client**.
- Names follow the disk's 8.3 rule. An upload with a long name or a space,
  such as `banana split.mod`, is stored under a short name like
  `BANANA~1.MOD`.
- All transfers are binary: files arrive exactly as they were sent.
- A transfer that receives nothing for 90 seconds is stopped, so a client
  that vanishes cannot hold the server.
- Speed depends on the machine and the **Buf** setting on the Control
  Panel's Ethernet page: a larger buffer is faster and uses more memory.
- The settings live in `SYSTEM/APPDATA/FTPD.CFG` on the disk the server was
  started from.
