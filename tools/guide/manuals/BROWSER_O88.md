# Browser

The Browser is a web browser for text and tables. It reads HTML pages from a
disk or fetches them over the network, and shows them with headings, lists,
bold, italic and underlined text, tables and simple forms. Links are
underlined: click one to follow it.

It does not show pictures, colours or styles. A picture appears as its
description, if the page gives one. It cannot open secure (`https`) sites by
itself, because the encryption they use would take minutes on this machine.
The os8088 web proxy, running on a modern computer, does that work for it.

![The Browser showing its own manual page, opened from the floppy.](shot:browser-window){full}

## Starting the Browser

The Browser is **BROWSER.O88** in the `APPS` folder. Double-click it to open
an empty Browser. Double-clicking any `.HTM` file also opens it, with that
page loaded. The `MEDIA` folder on the software disk has one to try:
[[BROWSER.HTM]], the Browser's own short manual. You can open more than one
Browser at a time.

Pages on a disk need nothing else. Pages on the network need a network
connection: see *Getting on the network* below.

## Reading a page

The window has a toolbar at the top, the location bar under it, and the page
below. The right end of the toolbar says what the Browser is doing: **Ready**,
**Looking up**, **Connecting**, **Requesting**, **Reading**, or the reason a
page did not load.

When the Browser opens, the location bar has the keyboard, ready for an
address. Press `Esc`, or click the page, to give the keyboard back to the
page. Then these keys move through it:

| Key | What it does |
|---|---|
| `Up`, `Down` | Scroll one line |
| `PgUp`, `PgDn`, `Space` | Scroll one window |
| `Home`, `End` | Go to the top or the bottom |
| `Tab` | Move through the address bar, the fields of a form and the links |
| `Return` | Submit the form you are typing in |
| `Esc` | Take the keyboard away from the location bar |
| `Ctrl+O`, `Ctrl+L` | Open a page, or go to the location bar |
| `Ctrl+S` | Save the page |
| `Ctrl+R`, `F5` | Fetch the page again |
| `Ctrl+A`, `Ctrl+C` | Select everything in the bar or the page, and copy it |
| `Ctrl+D` | Bookmark the page, or remove its bookmark |
| `Ctrl+U` | View the page's source |

**Help > Keyboard Shortcuts...** lists them.

The scroll bar on the right works with the mouse: click its arrows, click
above or below the thumb to move a window at a time, or drag the thumb.
Double-click the title bar to make the window fill the screen.

![The bottom of the same page: a form, and links.](shot:browser-scrolled){full}

Click a link to follow it. The toolbar's **Back** and **Fwd** buttons step
through the pages you have visited, and **Reload** fetches the current page
again. Back and Fwd are greyed when there is nowhere to go.

## Opening a page

![The File menu.](shot:browser-menu-file){right}

**File > Open...** opens a page from a disk with the standard Open dialog.

**File > Open Location...** puts the keyboard in the location bar. Type an
address and press `Return`. You can also click in the bar at any time and
type. An address looks like `http://example.com/page.htm`. You may leave out
the `http://`, and you may add a port number after the name, as in
`192.168.1.50:8088`.

The Browser follows a link from a page on a disk only if the link is a full
`http://` address. A link that only makes sense next to the page on its
server answers **Relative link: this page has no server**.

![An address typed on a machine with no network driver loaded.](shot:browser-nolink){full}

With no network driver loaded, an address answers **No link driver**, and
the page you were reading stays in the window, still scrolling and with its
links still working. See *Getting on the network* below.

## Secure sites

![Clicking an https link: the Browser refuses it straight away and says why.](shot:browser-https){full}

The Browser refuses `https` addresses at once rather than trying for minutes
and failing. Clicking such a link, or typing one into the location bar,
shows **Https is not supported**.

To reach secure sites, and most of today's web, run the **os8088 web proxy**
on a modern computer (Windows, macOS or Linux) on the same network. It has
its own manual. The proxy fetches the site, handles the encryption, and cuts
the page down to what the Browser can show. To use it:

1. Start the proxy on the modern computer. It listens on port 8088.
2. In the Browser, choose **File > Open Location...**, type that computer's
   address and the port, for example `192.168.1.50:8088`, and press
   `Return`.
3. The proxy's home page appears. Type an address or a search into it, or
   click one of its bookmarks.

Plain `http` sites, such as a server on your own network or FrogFind, need no
proxy.

## Forms

A form shows its text fields as boxes. Click one, or press `Tab` to reach it,
and type. Press `Return` or click the form's button to send it. Check boxes,
radio buttons and drop-down choices work too: click a choice to change it. A
password field hides what you type, and the Browser warns that the page is
not secure, because a password sent over `http` is not encrypted. A file field
shows **[unsupported]**.

## Links, selections and the right button

Drag across the page to select text. Click a link and press `Return`, or
press `Tab` until the link is chosen, to follow it from the keyboard. The
right button opens a small menu: on a link it offers **Open Link**, **Copy
Link** and **Save Link As...**, on selected text **Copy**, and elsewhere
**Save Page As...** and **View Source**. **View Source** shows the page as the
server sent it, and **Close Source** returns to the page.

## Bookmarks

Press `Ctrl+D` to bookmark the page you are reading, and again to remove its
bookmark. The **Bookmarks** menu lists up to eight saved pages: pick one to go
there. A page loaded from a disk cannot be bookmarked. Bookmarks are kept in
the file `BROWSE.DAT` in the `SYSTEM/APPDATA` folder and are there the next
time you start.

## Downloads

A link to something the Browser cannot show as a page, such as a text file, a
picture or an archive, opens the **Downloads** window instead of a page. It
asks where to save the file, then shows the file's name and address, how much
has arrived, a progress bar, the speed and the time left. **Cancel** stops the
transfer and removes the part that arrived. When the file is complete, **Close**
closes the window and the file stays where you saved it. **File > Downloads**
opens the window again.

## The menus

| Item | What it does |
|---|---|
| File > Open... | Open a page from a disk |
| File > Open Location... | Put the keyboard in the location bar to type an address |
| File > Save As... | Save the page as the server sent it, as HTML |
| File > Downloads | Show the Downloads window |
| Go > Top | Go to the top of the page |
| Go > Bottom | Go to the bottom of the page |
| Go > Reload | Fetch the page again, like the Reload button |
| History | The last eight pages you visited. Pick one to go back to it |
| Bookmarks | Your saved pages. Pick one to go there |
| Help > Keyboard Shortcuts... | The keys the Browser answers to |
| Browser > About Browser | Who made it |
| Browser > Close | Close this Browser |

The History menu says **(empty)** until you have visited a page.

## Getting on the network

The Browser, Telnet and the FTP Server reach the network through the
**Ethernet** driver, [[ETHER.DRV]]. It works with an **NE1000** or **NE2000** network card,
or a card that is compatible with one, which most clone cards of the period
are.

![The Control Panel's Drivers page. This machine has no card, so ticking Ethernet says No hardware found.](shot:browser-drivers){right}

1. Open the **Control Panel** from the menu at the left end of the menu bar
   and choose the **Drivers** page.
2. Tick **Ethernet**. The row says **Loaded** when the driver has found the
   card, or **No hardware found** when it has not.
3. Close the Control Panel. The machine remembers the choice and loads the
   driver at every start.

The driver looks for the card at the usual ports. If it cannot find yours,
click **Ports** on the Drivers page and set **Network card** to the card's
port. That takes effect at the next restart.

Once the driver is loaded, the Control Panel has an **Ethernet** page of its
own. It shows the card and its port, the card's hardware number (**Id**), the
machine's address (**Addr**), its **Router** and its name server (**Name**),
how the address was obtained (**Dhcp**), and counts of what has gone in and
out (**Rx**, **Tx**).

The machine normally gets its address by itself, from your router or another
DHCP server on the network. The **Dhcp** row says **Asking** while it waits,
**Bound** when it has an address, and **No server** if nothing answered. The
page has three buttons:

- **Renew** asks for an address again.
- **Set Up** opens the **Ethernet Setup** window. Choose **Manual** to type
  the **Address**, **Subnet**, **Router** and **Name** server yourself, for a
  network with no DHCP server or a cable straight to one other computer.
  Address and Subnet are required; leave Router or Name empty if there is
  none. **Ok** uses the settings at once, and they are saved when you close
  the Control Panel. Choose **Automatic** to go back to DHCP.
- **Buf** shows and changes how much memory each connection gets: **Buf 1K**,
  **Buf 4K** or **Buf 8K**. More is faster, and costs memory. It is greyed
  while a connection is open.

A DOS PC joined to this machine by the **Parallel link** ([[NET.DRV]]) can also carry the
Browser's connections, if that PC is on a network. The Browser uses a network
card first when it has both.

## Files

**File > Save As...** writes the page exactly as the server sent it, so you
can open it again later with **File > Open...** or by double-clicking it.
The toolbar says **Saved**, or **Save failed**. With no page loaded it says
**Nothing loaded to save**.

## Good to know

- A long page is kept for as long as the machine has memory to spare, and
  then cut off, with **Page truncated** in the toolbar. Past 400 links it says
  **Links limited**. A table shows at most eight columns, and a page at most
  eight forms.
- An address can be up to 159 characters long. A longer one answers **URL
  too long**.
- The Browser follows up to three redirects from a server. More answer **Too
  many redirects**.
- Pages made of frames are joined into one page, frame after frame.
- If a fetch fails, open the page you want again.
