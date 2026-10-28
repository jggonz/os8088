# HTTP drive

`HTTPFS.DRV` puts a folder published on a web server on the desktop as a
drive of its own. You open it, look through its folders, start programs and
read documents on it, and copy files from it, just as with a floppy. It is
read-only: nothing can be written, renamed or deleted on it.

It is how a machine with a network card can install os8088 straight from a
web address, with no floppies to write first.

## What it needs

- A network: the **Ethernet** driver ([[ETHER.DRV]]) with a working card, or
  the parallel cable's network driver.
- A web server that publishes the folder, with an index file called
  `OS88.IDX` beside it. The index lists every file and folder with its size.
  The drive reads it once and answers every listing from it, so browsing the
  folders costs nothing on the network. A helper program on a modern
  computer writes the index, and can serve the folder as well.
- An address that starts with `http://`. Secure `https://` addresses are
  refused.

## Turning it on

Open the **Control Panel**, choose **Drivers**, and tick **HTTP Drive**. A
**Web Drive** item appears in the Control Panel's list, and that page is
where the address goes and the drive is mounted. The page is described
under [[HTTPPAGE.DRV]].

## Using the drive

The drive appears on the desktop labelled **HTTP**, with the next free drive
letter. Nothing is fetched until you open it. Opening it reads the index;
after that only the files you open or copy go over the network, and each
one is fetched when it is wanted.

Packed files and packed programs open as they would off a floppy.

## Remembered across starts

When you close the Control Panel, the address and whether a drive was
mounted are written to `HTTPDRV.CFG` on the system disk. At the next start
the drive is put back on the desktop with that address, still unread until
you open it.

## Good to know

- A file that changed on the server after the index was read fails to
  open, rather than handing over different bytes. Unmount and mount again
  to read a fresh index.
- The server must answer plain `http://` requests. Redirects are not
  followed.
- A slow or busy server makes a file take longer to open. If nothing arrives
  for ten seconds, the open fails with **Timed out** on the Web Drive page.
