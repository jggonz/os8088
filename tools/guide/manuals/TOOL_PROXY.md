# Web proxy

The os8088 web proxy runs on your modern computer and lets the os8088
Browser reach today's web. It fetches a page for os8088, handles the
encryption, and cuts the page down to what the Browser can read and show.

The Browser cannot open `https` sites itself: the encryption would take
minutes of work for each page on a 4.77 MHz PC. Most modern pages are also
hundreds of kilobytes of markup and scripts that the Browser has no use for.
The proxy removes the menus, adverts and scripts, keeps the text, links,
tables and forms, and splits a long page into pages small enough for the
Browser.

There are three ways to run it: **os88proxy.py** on the command line,
**os88proxygui.py** for the same proxy in a window, and **os88proxy.exe**,
the window as a single Windows program that needs no Python.

## What it needs

- A computer running Windows, macOS or Linux, on the same network as the
  os8088 machine.
- Python 3.8 or newer. Nothing else: no extra packages. The window also
  needs Tk; on Linux install the `python3-tk` package.
- The `.exe` needs nothing at all. Copy the one file anywhere and
  double-click it.
- The os8088 machine needs a network card, its driver ([[ETHER.DRV]]) and the
  Browser ([[BROWSER.O88]]).

To build the `.exe` yourself, on a Windows computer with python.org's
Python (with "Add python.exe to PATH" ticked), run
`tools\build-os88proxy-exe.bat` from the top folder of the os8088 source.
It installs PyInstaller, checks the proxy, and writes `dist\os88proxy.exe`.

## Starting the proxy

On the command line: `python3 tools/os88proxy.py`

It prints the address to type on os8088, for example
`on os8088:  Browser, File > Open Location...  ->  192.168.1.50:8088`. Press `Ctrl+C`
to stop it.

In the window, click **Start**. The state beside it changes from **Stopped**
to **Running**, and the box under it shows the address to type, in large
letters, with a **Copy** button. Click **Stop** to stop. The window
remembers its settings for next time.

On Windows the first start raises a firewall question. Allow the proxy on
private networks only.

## Using it from os8088

1. In the Browser, choose **File > Open Location...**.
2. Type the proxy's address and port, for example `192.168.1.50:8088`, and
   press `Return`.
3. The proxy's home page appears.

The home page has:

- **Back** and **About** links.
- An **Address** box: type a site, such as `en.wikipedia.org`, and click
  **Go**. You do not need to type `https://`.
- A **Search** box: type words and click **Go** to search the web. The
  proxy uses DuckDuckGo unless it was started with another search.
- **Bookmarks**: Reddit front page, r/vintagecomputing, FrogFind (search for
  old machines), Wikipedia, Hacker News and os8088. The proxy can be given
  your own list in place of the last four.
- A line saying how many pages it has served, how much came in and went
  out, and how long it has been running.

Every page the proxy sends starts with a **Home** link and a **Full** link.
**Full** shows the same page with less taken out, for when the proxy has
removed something you wanted. A long page ends with **page 1 of 3**,
**Next >** and **< Prev** links, and the name of the site.

Links on a page lead back through the proxy, so you can follow them as
usual. Pictures appear as their caption in square brackets.

You can also type an address straight into the Browser's location bar:
`192.168.1.50:8088/u/en.wikipedia.org/wiki/IBM_PC` opens that `https`
page, and `/h/` in place of `/u/` opens a plain `http` one.
`192.168.1.50:8088/rd/r/vintagecomputing` opens a Reddit forum.

Plain `http` sites, such as a server on your own network or FrogFind, work
without the proxy.

## The window

| Control | What it does |
|---|---|
| **Start** / **Stop** | Starts and stops the proxy |
| The address box and **Copy** | What to type in the Browser's Open Location; Copy puts it on the clipboard |
| **Sites visited** | A line for every page: the site, how big it was, how big it became, how many pages, and how long it took. Failures are in red |
| **Show technical detail** | Shows the full log in the list as well |
| **Clear** | Empties the list |

The **General** tab:

| Setting | What it does |
|---|---|
| Port | The port os8088 connects to. 8088 unless you change it |
| os8088's screen | CGA - 79 columns, Hercules - 89 columns or VGA - 79 columns. Only changes where code blocks and wide tables are wrapped |
| Page size | 12 KB - fastest, 22 KB - a good default, or 31 KB - the browser's limit. Bigger pages mean fewer of them and a longer wait for each |
| Strip the page | Full - drop menus and adverts, or Light - keep more of it |
| Tables and columns | Rearrange into tables, or Leave them alone |
| Pictures | Show the caption, or Leave them out |
| Save pages to | A folder. Each page is saved there as the site sent it and as os8088 got it, which is what to send back when a page comes out wrong |
| Only allow | The addresses allowed to use the proxy. Blank means anyone who can reach the port. `192.168.1.*` allows a whole network |
| Address to advertise | Leave blank unless os8088 reaches this computer by another name |
| Check every page against the browser's limits | Leave this on. It catches a page os8088 would read wrongly without saying so |

The **Reddit** tab:

| Setting | What it does |
|---|---|
| Reddit source | Reddit's own JSON feed (needs no account), or The web page |
| Reddit | www.reddit.com - no sign-in needed, or old.reddit.com - wants a session |
| Reddit replies | How a reply tree is drawn: Quote marks (>), Bars, A line between threads, or A table. The table is the easiest to follow and the slowest to draw |
| Reddit reply indent | None - flat, 1 character or 2 characters |
| Cookies file | A `cookies.txt` exported from a desktop browser that is signed in to the site. This is the only way to be signed in |

## Command-line options

`python3 tools/os88proxy.py --help` lists them all with their defaults.

| Option | What it does |
|---|---|
| `--port N` | The port to listen on. Default 8088 |
| `--bind ADDR` | The network address to listen on. Default `0.0.0.0`, every network the computer is on. `127.0.0.1` refuses everything but this computer |
| `--allow ADDR` | Only these clients may use the proxy. Give it more than once for more; a trailing `*` matches a range, such as `192.168.1.*` |
| `--advertise ADDR` | The address to print at start-up |
| `--public-base URL` | The address forms on a page send to, such as `http://192.168.1.50:8088` |
| `--quiet` | Print less |
| `--cols N` | The Browser's text columns: 79 for CGA and VGA, 89 for Hercules |
| `--budget BYTES` | The page size before a page is split |
| `--strip full` or `light` | How much is removed from a page |
| `--tables auto` or `off` | Whether layouts are rearranged into tables |
| `--images mark` or `drop` | Show picture captions, or leave pictures out |
| `--emphasis stars` | Put asterisks round bold text |
| `--forms all` | Let the proxy send forms that post their data. Without it such a form shows **[form: POST is not enabled - run with --forms all]** |
| `--sites off` | Turn off the special handling for Reddit |
| `--reddit-api`, `--reddit-site`, `--reddit-thread`, `--reddit-indent` | The Reddit settings above |
| `--reddit-maxcomments N` | The most comments shown from one thread. Default 200 |
| `--timeout S` | How long to wait for a site. Default 20 seconds |
| `--cookies FILE` | A `cookies.txt` from a signed-in desktop browser |
| `--insecure` | Do not check sites' certificates, for a machine on your network with a self-signed one |
| `--search URL` | The search address, with `%s` where the words go |
| `--bookmarks FILE` | Your own home-page bookmarks, one `Name  URL` per line. Lines starting with `#` are skipped |
| `--save-dir DIR` | Save every page to this folder |
| `--render URL` | Fetch a page and print what os8088 would receive, without serving anything |

## Good to know

- **Anyone who can reach the port can use the proxy** to fetch any web
  page through your computer. On a home network that is usually fine. On a
  shared network, use **Only allow** (`--allow`) with the os8088 machine's
  address.
- The connection between os8088 and the proxy is plain `http` and is not
  encrypted. Only the proxy's own connection to the site is.
- With a cookies file the proxy is signed in as you, for everyone who can
  reach it. The os8088 machine never holds your password.
- A page keeps one form, with up to four text boxes. A second form shows
  **[a second form is not shown...]**.
- A password field on a page cannot be used from os8088.
- The proxy remembers the links it has handed out, in a file in your home
  folder (`.os8088proxy`, or `os8088proxy` in your application data folder
  on Windows). If it forgets one, the page says **Link expired**: go Home
  and start again.
- A page the proxy cannot fetch comes back as a short page saying why, such
  as **Cannot reach it** or **Timed out**.
- If the window ever reports an internal error, the details are in
  `crash.log` in the same settings folder.
