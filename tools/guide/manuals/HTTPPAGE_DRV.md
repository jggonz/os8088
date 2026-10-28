# HTTP drive settings

`HTTPPAGE.DRV` is the **Web Drive** page of the Control Panel. The drive
itself is [[HTTPFS.DRV]]. This file holds only the page you set it up with:
the web address, and the buttons that put the drive on the desktop and take
it off again. It is read from the system disk when you choose the page, and
let go when the Control Panel closes.

## Reaching it

Open **Control Panel > Drivers** and tick **HTTP Drive**. A **Web Drive**
item appears in the Control Panel's list; scroll down to it.

## The page

- **Address**: the web address of the folder to mount, for example
  `http://10.0.0.2:8088/`. Click in the box and type. It must start with
  `http://`.
- **Mount** puts the drive on the desktop with its own letter. Pressing
  **Enter** in the box does the same. The page fetches the folder's index
  straight away and says **Fetching...** while it does.
- **Unmount** takes the drive off the desktop.

Under the buttons the page says which drive is mounted and how many entries
its index holds, **not read yet** when the drive has not been opened, or
**Not mounted**. The line below it names the last thing that went wrong:

| Message | What it means |
|---|---|
| **Bad address** | The address is not `http://host/...` |
| **No network** | No network driver is loaded, or it has no connection |
| **Cannot connect** | The server refused, or its name was not found |
| **Timed out** | Nothing arrived for ten seconds |
| **HTTP** and a number | The server answered with that error, such as 404 |
| **No OS88.IDX** | The folder has no index the drive can read |
| **Cut short** | A file ended before its size |
| **No memory** | There was no room for the index |
| **No drive letter** | Every drive letter is taken |

## Good to know

- The address and whether the drive is mounted are saved when the Control
  Panel closes, so the drive comes back at the next start.
- If the page cannot be read from the system disk, it shows **Needs the
  system disk**. Put the system disk back in and choose the page again.
