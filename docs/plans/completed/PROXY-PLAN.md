# The reformatting proxy — `tools/os88proxy.py`

**This document is the design; `tools/os88proxy.py` is the code and
`--selfcheck` is the gate.** It is host-side tooling in the sense
`tools/htmsim.py` is: nothing here runs on the 8088, nothing here ships on a
floppy, and no kernel interface changes — so SPEC.md is untouched and this
file, like docs/plans/completed/BROWSER-PLAN.md, is where it is written down.

docs/plans/completed/BROWSER-PLAN.md §8.3 named a reformatting proxy as "a first-class
configuration, not a workaround" and left it as staging step 6. This is that
step.

---

## 0. The verdict, up front

**Three jobs, and only one of them is about bandwidth.**

1. **Terminate TLS.** SPEC.md §71.2: an RSA-2048 private operation is *minutes*
   on a 4.77 MHz 8088, so `br_split` refuses `https://` by name. Essentially
   the whole modern web is behind it. The handshake happens here or not at all.
2. **Strip.** A modern page is 200 KB of markup that renders to two screens of
   text. Measured on real pages through this tool: **pypi.org 21.8 KB → 705
   bytes**, **its `requests` project page 177.6 KB → 17.3 KB in two pages**,
   the `tests/proxy/modern.htm` fixture **4.9 KB → 2.1 KB**. On the parallel
   cable at PERFORMANCE.md Set 39's measured 3,741 B/s that is the difference
   between 55 seconds and 2.
3. **Hold the session.** The proxy signs in and keeps the cookie; os8088
   sends an ordinary unauthenticated `GET`. **os8088 never holds a
   credential** — BROWSER-PLAN §8.3.1's rule, and the reason there is no
   password field in the browser (§7.1 there, SPEC.md §71.2 here).

**And a fourth that BROWSER-PLAN did not have to think about, which turned out
to be the one that shapes the whole design: the browser has hard limits and
every one of them fails in SILENCE.**

| what | limit | where | what exceeding it does |
|---|---|---|---|
| request path | **95 bytes** | `BR_PATHMAX` | `br_split` truncates and fetches **something else** |
| host | 63 bytes | `BR_HOSTMAX` | truncated |
| a composed URL | 159 bytes | `BR_URLMAX` / `BR_UBUF` | truncated |
| response body | **32,767 bytes** | `BR_MAXKB`, `br_take` | cut **mid-tag**; the rest of the page is read as an attribute |
| anchors on a page | 200 | `BR_LNKMAX` | drawn, not clickable |
| bytes of href on a page | 6,144 | `BR_LINKKB` | the ones past the arena are drawn, not clickable |
| display lines | 1,365 | `BR_LINEKB` | the tail of the page does not exist |
| text inputs on a page | **4** | `BR_FMAX` | the fifth is not in the query |
| hidden pairs, encoded | 95 bytes | `BR_HIDMAX` | silently short query |
| form action | 63 bytes, **and absolute** | `BR_ACTMAX` + `br_split` | a root-relative action parses as host `f`, path `/tok` |
| forms on a page | **1** | `br_form` zeroes `[br_fn]`/`[br_hn]` | the second takes the first's fields |
| characters | ASCII 0x20..0x7E | SPEC.md §6.1 | `font_char` indexes past its 95 glyphs |
| tags | `br_tagtab` + `h1..h6` | `br_act` | an unknown tag is **ignored**, so `<section>`'s paragraph break is lost |

Not one of those produces an error message on os8088. That is what §5's gate
is for, and it is the part of this tool most worth keeping working.

---

## 1. Where it runs, and what it needs

**Windows, macOS or Linux; `python3` and the standard library.** No pip, no
`requests`, no BeautifulSoup — the same rule the rest of `tools/` follows, for
the same reason: the toolchain is `nasm` + `python3` and a proxy that needs a
package manager is a proxy that stops working on the machine it was installed
on two years ago.

```sh
python3 tools/os88proxy.py                       # serve on 0.0.0.0:8088
py -3 tools\os88proxy.py                         # ...the Windows spelling
python3 tools/os88proxy.py --selfcheck           # the gate; no network needed
python3 tools/os88proxy.py --render URL --cost   # what os8088 would get,
                                                 # and what it costs to DRAW
```

On os8088: **Browser ▸ Open Location**, type `192.168.1.50:8088`, Return.
That is the whole of the guest-side configuration, and it is why §2 is shaped
the way it is.

`make` does not build it, `all` does not run it and no image carries it.

---

## 2. It is an ORIGIN server, not a CONNECT proxy

The browser has no proxy setting. It sends origin-form requests — `GET /path
HTTP/1.0` with a `Host:` header — because that is what `br_req` composes, and
a real proxy would want absolute-form and a configuration cell to hold the
proxy's address. **Serving as an origin server needs no guest change at all**,
and a guest change is the expensive kind.

So every URL is a path on this machine, and **§0's 95-byte path is what the
URL space had to be designed around**:

| route | what it is |
|---|---|
| `/` | home: the search box and the bookmarks |
| `/l/<tok>` | a link on a rendered page. **11 bytes.** `?r=1` re-fetches |
| `/p/<tok>/<n>` | page *n* of a paginated document, out of the cache |
| `/x/<tok>` | the same page with every heuristic **off** — the escape hatch |
| `/f/<tok>?a=b` | a form submit; the proxy issues the real `GET` or `POST` |
| `/u/host/path` | typed by hand, `https` assumed |
| `/h/host/path` | ...and the plain-`http` spelling |
| `/rd/<sub>` | `old.reddit.com/r/<sub>/`, for typing; `/rd` is the front page |
| `/s?q=` | search |
| `/b` | back. **Not in the page chrome any more** — §12 |
| `/in`, `/li`, `/out` | sign in, and sign out again (§11) |
| `/about` | what this proxy thinks it is doing |

**A link is a minted token because the real URL does not fit.**
`https://www.reddit.com/r/vintagecomputing/comments/1abcdef/a_long_title/` is
72 bytes of path before the host is counted; through `br_split` it is a
truncated request for a page that does not exist. `/l/6q4mzt3a` is eleven.

**And the arena is the second reason a link is a token.** Every href on a page
is packed into `BR_LINKKB` — six kilobytes — so 190 anchors of `/l/6q4mzt3a`
is 2.3 KB of it, where 190 real URLs would be 15 KB and everything past the
arena would be drawn and not clickable.

Tokens are a **hash of the URL, not a counter**, so they survive a restart of
the proxy: a page left on os8088's screen overnight still has working links
in the morning. They are 8 base32 characters and **a collision extends rather
than overwrites** — two pages sharing a token is the user sent silently to the
wrong page, which is the failure this whole document is about. The store is a
JSON file under `~/.os8088proxy/`, readable in a text editor when something
goes wrong, for the same reason `tools/os88disk.py` writes a deterministic
image.

---

## 3. The pipeline

```
  fetch          tree            transforms          emit           paginate
  (TLS, gzip,    (a real tree,   (junk, main,        (the guest's   (32KB and
   redirects,     because the     tables, links,      dialect and    1,365 lines
   cookies)       heuristics      folding)            ONLY that)     are cuts)
                  need one)
```

**The tree is the one place this differs from the browser in kind.** The
guest's own parse is a linear byte scan and is right to be
(BROWSER-PLAN §13.1); *"is this `<div>` a column of a layout"* and *"which subtree is the
article"* are questions about children, and a stream has none. It is thrown
away the moment the page has been emitted.

### 3.1 What is dropped, and what is turned into what

| in | out | why |
|---|---|---|
| `<script>`, `<style>`, `<svg>`, `<noscript>`, `<iframe>`, `<video>` | nothing | the guest drops the first three itself; the rest it cannot show |
| `<nav>`, `<header>`, `<footer>`, `<aside>`, `class="...sidebar/cookie/share/advert..."` | nothing | furniture. **`Full` brings it back** |
| `<section>`, `<article>`, `<main>`, `<figure>`, `<details>` | `<p>`-separated blocks | **the important one**: the guest IGNORES these, so their text runs together into one block with no paragraph break |
| `<img alt="...">` | `[alt]` | the parser reads no attributes, so `alt` has to become text or it is nothing |
| `<img>` with no alt | **nothing at all** | measured: PyPI's project page has 170 of them, and `[image]` on 170 lines is a third of the page and 12 seconds of CGA drawing to say there were pictures |
| `<ol><li>` | `<li>1. …` | the guest has one bullet and no numbering |
| `<blockquote>` | `<p>&gt; …` | it draws a blockquote as a bare paragraph break, so the mark has to be characters |
| `<pre>` | `<pre>`, hard-wrapped to `--cols` | `TA_PRE` is the *no-wrap* marker: a 120-column code block is simply not there on a 79-cell screen |
| `<dl>` | a two-column table | see §4 |
| a CSS row of `<div>`s | a table row | see §4 |
| a layout `<table>` | unwrapped | see §4 |
| `&…;`, UTF-8, Latin-1 | ASCII 0x20..0x7E | §3.2 |
| colours, `style=`, `class=`, `colspan` | nothing | BROWSER-PLAN §2.2.1, and `colspan` is read by nothing on the guest |

### 3.2 The fold has ONE definition and it is `tools/htmsim.py`'s

`htmsim.FOLD` is what `apps/browser/browser.asm`'s `br_l1tab` is generated
from (`--emit-l1tab`). The proxy imports it rather than carrying a table of
its own, because two hand-maintained fold tables drift on the first accented
letter nobody checked, and the failure is a glyph index past the 95 the font
has. If `htmsim` cannot be imported — the file copied away from the repo —
the proxy says so at startup and on `/about`, and `--selfcheck` **fails**.

Entities are resolved here, and the only three the proxy emits are `&amp;`,
`&lt;` and `&gt;`. `br_enttab` knows sixty; emitting any of the others would
be asking the guest to redo work already done.

### 3.3 Boilerplate and the main article

Readability-lite: `<main>`, then `<article>`, then the highest-scoring
container, where score is *text − 3 × link text + 40 × paragraphs*, with a
bonus for `content`/`article`/`post` in the class. **A candidate must hold a
quarter of the document's text before it may win**, because a wrong pick loses
the page and the fallback — the whole body — is merely verbose.

Every heuristic in this document has the same escape hatch: **the `Full` link
in the page footer** (`/x/<tok>`) re-renders with the boilerplate kept and
nothing invented. That is not politeness; a heuristic with no way past it is a
page the user cannot reach.

---

## 4. Divs, CSS and tables

The ask was "reformat divs and CSS and other formatting into simple tables",
and the honest version of it is three transforms, because **there is no CSS
engine here and there is not going to be one**. The evidence available is the
class name, an inline `display:`, and the *shape* of the children.

**Layout tables are unwrapped.** BROWSER-PLAN §3.2.1's heuristic, which
`tools/htmsim.py` already implements, is the one this tree settled on — a 1×1
table is a block and a table with no text is decoration — plus two the modern web needs: `role="presentation"` says so
itself, and a table whose cells each hold their own block structure is a page
skeleton. This runs even under `Full`, because leaving it in makes the whole
page one table cell and a cell is truncated.

**A CSS row of `<div>`s becomes a table row.** The container must say so
(`display:flex|grid`, or a class matching `row`/`flex`/`grid`/`col-N`/`card`/
`tile`), have 2..`--maxcols` element children, and every child must be
non-empty and under `--cellmax` characters. Where the evidence is absent the
divs stay divs and become paragraphs — which is what they would have been
anyway, so a miss costs nothing.

**`<dl>` becomes two columns**, term and definition, which is what a
definition list has always been drawn as everywhere except HTML.

**A table too wide is turned on its side.** Six columns on a 79-cell screen is
thirteen characters each, which is not a table, it is a mess: past
`--maxcols` each row becomes one paragraph of `header: value | header: value`.

Measured, `tests/proxy/modern.htm` on a CGA through `htmsim --render`: the
three `.card` divs come out as a three-column table, the capacitor table stays
a table, the `<dl>` of parts becomes two columns, and the `role=presentation`
table wrapping the article and its sidebar disappears.

---

## 5. The gate

`--selfcheck` and — this is the part worth defending — **every real answer the
server sends**, because the failures in §0's table cannot be reported from the
os8088. It checks:

* every byte is ASCII the cell font has a glyph for;
* every tag is in `br_tagtab` (or `h1..h6`, or the `<html>`/`<body>` wrapper
  the guest ignores) and every attribute is one the guest actually reads;
* no `https://` href survived;
* every path fits `BR_PATHMAX`, every host `BR_HOSTMAX`, every URL `BR_URLMAX`;
* the body fits `br_take`'s 32,767 and the page fits the 1,365-line table
  (through `htmsim.layout`, so it is the same measurement the browser's own
  model makes);
* anchors ≤ 200, forms ≤ 1, text inputs ≤ 4, hidden pairs ≤ 95 bytes, the
  action absolute and ≤ 63;
* and, for a first-class site, **the SHAPE** — §6.2. Legality is not enough:
  a page can be perfectly legal and be the wrong page, which is this tree's
  own lesson about two identical nothings agreeing perfectly.

**And it re-reads `apps/browser/*.asm` and refuses to agree with itself if a
constant has moved.** That is what makes the numbers safe to copy into a
host-side tool: the guest is free to change, and this file finds out on the
next `--selfcheck` rather than on the next page that renders wrong.

`tests/proxy/*.htm` are the fixtures; `tests/htm/*.htm` (BROWSER-PLAN's own)
are run through it too, so a page the browser already renders must survive the
proxy unharmed.

**`python3 tests/proxytest.py` is the other half, and it is the one that
matters more**: `--selfcheck` checks the render, and this checks the SERVER —
it speaks `br_req`'s exact request down a real socket, reads until close the
way `br_take` does, refuses any path `br_split` would truncate, follows every
link the way `br_resolve` resolves it, and composes a submit the way
`br_submit` composes it. A page that is perfect and arrives down a socket the
browser cannot drive is not a page. It carries its own origin server and its
own proxy, so it needs no network and no emulator, and it runs in a tenth of a
second.

---

## 6. First-class sites, and Reddit

**Phase 2 in the ask, and the seam is built now rather than retro-fitted.** A
handler is a class with three methods and one line in `SITES`:

```python
class Site:
    hosts = ("example.com",)
    def upstream(self, url):  ...   # what to actually FETCH
    def transform(self, text, url, opts, links, selfbase):
        ...                         # -> (title, blocks, ctx), or None
```

Three rules, and the third is what keeps it honest:

1. **`upstream` rewrites the URL before the fetch** — which is how
   `www.reddit.com` becomes `old.reddit.com`, whose markup is server-rendered,
   small and stable.
2. **`transform` returns what the generic pipeline returns**, so pagination,
   the chrome and the gate know no difference.
3. **A handler that does not recognise what it was given returns `None` and
   the generic pipeline runs.** A site redesigns overnight; a proxy that
   answers "cannot parse" to a page it merely stopped recognising is worse
   than one that shows the plain reduction.

### 6.1 Reddit, built against real captures

`tests/proxy/reddit-front.htm` and `reddit-thread.htm` are pages saved from a
signed-in old.reddit session — 170 KB and 631 KB — scrubbed by
`tests/proxy/scrub.py` and committed, so this is written against the markup
rather than against a memory of it. What comes out is what was asked for and
nothing else.

**A listing is the categories row, and then two lines a post:**

```
Home Popular All Saved hot new top rising

Using H3 as a Character Reference Sheet Generator
165 comments  r/StableDiffusion

CDC: U.S. kindergarten vaccine exemptions hit record high as coverage falls
nationwide
1124 comments  r/news
```

169.5 KB in, **5.9 KB out**, 112 display lines. No rank, no score, no author,
no time, no thumbnail, no flair, no domain, no button row.

Four things about it are load-bearing. **The `data-` attributes are the
interface**: every `div.thing` carries `data-permalink`,
`data-comments-count`, `data-subreddit-prefixed`, `data-score` and
`data-url`, which is where all of this comes from — the score in the DOM is
*three* `score` divs (one below, one at, one above the truth) inside a
`midcol unvoted`, and the comment count is a sentence. **The DOM is the
fallback** for each of them, because an attribute that goes away must not take
the link with it. **The title links to the post's own destination and the
count links to the thread** — pointing both at the thread would waste one of
two links on a 95-byte path. And **a post is ONE `<p>` with a `<br>` in it**,
not two paragraphs: the guest puts a blank line between paragraphs, so two
would double the height of a listing and cut a title off from its own count.

**The categories row is ours, not the page's.** A signed-in front page puts
`my subreddits` and a hundred-name dropdown where a logged-out one puts
POPULAR / ALL, so scraping it gives a different row depending on who is
looking; Home / Popular / All / Saved always mean the same thing. The sorts
(`hot new top rising`) are appended to whatever listing you are on.

**A thread is the title, the group, the post, a rule, and then the comments:**

```
Woman dies, comes back to life, gets parking ticket
r/nottheonion
https://www.stuff.co.nz/nz-news/361021333/woman-dies-comes-back-lif...
-------------------------------------------------------------------------------
SpaceJackRabbit 1842 points 14 hours ago
My wife drove herself to the hospital to give birth as she was in labor
already. Thankfully hospital staff parked her car.

> justabill71 737 points 14 hours ago
> My dad took my mom into the hospital, then went to park the car. My sister
was already born by the time he got back.

>> animal_chin9 318 points 12 hours ago
>> Meanwhile my dad asked my mom if he could stop and get some coffee...
```

631 KB in, **40 KB over two pages**, 470 and 479 lines. The title is emitted as
`<h1>` so §3's de-duplication folds it into the page title and it is drawn
once.

**The indent is characters, and it is one per level** (`--reddit-indent`,
`--reddit-maxindent`). The guest draws `<blockquote>` as a bare paragraph
break — there is no indent to be had — so depth has to be ink, and a nested
box would have squeezed the fourth reply into a column on the left. One `>`
costs a character a level and leaves 70 for the words. A wrapped line loses
the prefix, which is the price of the guest owning the wrap; `--reddit-indent
0` turns it off entirely.

**The header line and the first paragraph are one `<p>` with a `<br>`**, for
the listing's reason: a blank line between a name and what it said doubles the
height of a thread.

### 6.2 What is verified, and what is not

The rendering is: `--selfcheck` runs both captures through the handler and
asserts the SHAPE — 25 posts each a title line and a `N comments  r/group`
line, no table, the categories row present, no tagline fluff; and for the
thread a rule, 40+ `author points ago` taglines, and something indented two
levels deep. Those checks were confirmed to FAIL when the handler is turned
off (`--sites off`) and when the indent is removed, because a check that
cannot fail is not a check.

`tests/proxytest.py` runs the whole route over a socket in the guest's wire
format against a local stand-in that answers the legacy login endpoint the way
reddit does and serves the real captures: sign in wrong, sign in right, get
the listing, click a comment count, read the thread.

**What is NOT verified is the live site**, and it cannot be from here — this
container's network policy refuses reddit.com outright. The markup is a real
capture and the login protocol is the documented legacy one, but the first
thing to do on a real network is to sign in once and read a listing.

## 7. Forms, and the one capability the proxy adds

The guest can only `GET`, and it composes `action` + `?` + encoded pairs
verbatim. So:

* **the action is rewritten to `/f/<tok>` on this proxy** — absolute while it
  fits `BR_ACTMAX`'s 63 bytes and relative after that (`--form-action`), which
  §12 explains: a composed action goes through `br_resolve` now, so a rooted
  action is legal where it used to parse as a host called `f`;
* **a `POST` form becomes a `GET` through the proxy**, which then issues the
  real `POST` upstream (`--forms all`). That is the one thing here that gives
  the guest a capability it does not have;
* `<select>` collapses to its selected value as a hidden field; `password`,
  `file`, `checkbox`, `radio` and `textarea` are refused **with a visible
  marker**, never silently — SPEC.md §47 rule 3's say-why-not, because a form
  that quietly lost a field submits the wrong query;
* everything past the fourth text field, the 95th byte of hidden pairs or the
  first form is dropped **and counted in the page**.

**One limit here the proxy cannot enforce, because the guest composes the
URL**: a long thing typed into a search box makes a long query, and `br_uput`
stops at `BR_URLMAX`. A ~40-character search term is the practical ceiling
with a short proxy address, and past it the request is truncated rather than
refused. Shortening the proxy's own address (`--public-base` with an IP) is
what buys room, and it is the reason the token in `/f/<tok>` is eight
characters and not sixteen.

---

## 8. What I recommend next, in the order I would do it

1. **Sign in once on a real network** (§6.2, §11). The markup is captured and
   the protocol is the documented one; the handshake itself has never run
   against reddit.
2. **A real cache on disk**, not just in RAM. Back and Next are free today
   only inside one run of the proxy; a disk cache also gives an offline mode,
   which on a machine that shares one desk with the router is worth more than
   it sounds.
3. **Images to GIF.** os8088's Paint reads GIF (SPEC.md §42). A route that
   transcodes an image to the screen's geometry and 2 or 16 colours would let
   os8088 *see* a picture for the first time — it cannot be drawn in the
   browser, but it can be fetched and opened. It needs Pillow, so it must be
   optional and degrade to today's behaviour when Pillow is absent.
4. **A `--profile` per adapter.** `--cols 79` is a CGA; Hercules is 89 and its
   window is 36 rows. The proxy already takes the number; what is missing is a
   way for os8088 to *say* which it is, and the cheapest version is one link
   on the home page per adapter.
5. **Per-client history.** One stack today, on BROWSER-PLAN §8.3.1's
   single-user premise. Four more lines makes Back correct with two machines
   on the bench.
6. **`gzip` between the proxy and os8088** is the one thing I would NOT do,
   and BROWSER-PLAN §8.1 already priced it: inflate is ~1.5 KB of guest code
   and a 32 KB window on a machine whose whole source claim is 32 KB. The
   proxy has already removed 90% of the bytes; the remaining win is not worth
   a decompressor in the browser.
7. **A `Save to A:` route** — the proxy writes the page as a `.HTM` file into a
   floppy image, so a long article can be read without the wire. It borders on
   `tools/os88disk.py`'s job and belongs there rather than here.

**An upstream error is passed through, body and all.** A 404 has a page on it
and it is usually the site's own words; the browser reads the code off the
status line (`br_hdrb`) and shows the body either way, so replacing it with a
notice of our own would lose the only useful half. The proxy's own notices -
a dead token, an unreachable host, a page that is not a page - are the ones it
writes itself.

---

## 9. What it deliberately does not do

* **It is not the renderer.** The browser parses HTML itself; on the Ethernet
  path there may be no proxy at all, and a browser that renders one server's
  dialect is not a browser (BROWSER-PLAN §8.3.1). Everything here is an
  accelerator and a TLS bridge.
* **It does not hold state for the browser.** No per-session anything the
  guest could come to depend on: a token store and a page cache, both of which
  may be lost between one page and the next without the browser noticing
  anything worse than "Link expired".
* **It does not execute JavaScript.** A page that renders nothing without it
  renders nothing here. A headless browser would fix that and would also bring
  a package manager, a browser engine and 400 MB — the answer for those sites
  is a first-class handler that reads the site's own JSON, which is §6's seam.
* **It does not follow `robots.txt`.** It is one person's browser, driven by
  one person's clicks; it is not a crawler and must not become one.

---

## 10. Security, and the reason to read this section

**This proxy fetches any URL anybody who can reach the port asks it to**, from
inside the owner's network, and it may hold a signed-in cookie jar while doing
it. That is a real capability to leave listening on 0.0.0.0.

* `--bind 127.0.0.1` when it is only ever driven from the same machine.
* `--allow 192.168.1.*` (repeatable) to name the machines that may use it.
* the cookie jar is a **credential on disk**: `--cookies` reads whatever the
  file holds, so give it file permissions and keep the proxy single-user, per
  BROWSER-PLAN §8.3.1.
* On Windows the first run raises a firewall prompt. **Private networks
  only.**

os8088 side is the same argument from the other end: there is no TLS between
the proxy and the guest, and there is no need for one — the traffic is a plain
`GET` for a page the proxy is about to fetch anyway, and the credential never
leaves the proxy.

---

## 11. Signing in — REMOVED (see §19)

> **This section describes a feature that is gone.** The sign-in form, the
> `/in`, `/li` and `/out` routes, the legacy `/api/login` call and `whoami`
> were removed in §19: reddit's legacy endpoint stopped answering JSON, the
> modern flow has a captcha in it, and reading reddit needs no account at all
> (§16). **A cookies.txt is the only way to be signed in now**, which asks
> nothing of the browser and keeps no credential anywhere. What is below is
> kept because the ARGUMENT about where a password may travel is still the
> right one, and the next site that tempts somebody to build this again should
> have to read it first.

**The credential is plain between os8088 and the proxy, and HTTPS from there
on.** That is the owner's decision and it is the only shape that works: there
is no TLS on the wire the browser draws on, and there is no password box in
the browser at all — `type=password` is refused (BROWSER-PLAN §7.1) precisely
so that a masked box cannot promise what the machine cannot keep. So the
sign-in page uses two ordinary text fields and says so on the page.

| route | what it does |
|---|---|
| `/in` | the sign-in page: `User`, `Password`, one submit. Two fields, one form — the guest's limits exactly |
| `/li?u=&p=` | signs in, and **answers with the LISTING** |
| `/out` | empties the cookie jar and the page cache |

**A success page is not served, and that is deliberate**: the only thing it
could say is what the listing says by existing, and it would cost a fetch and
a screen to say it. A *failure* comes back as the sign-in page again with the
reason on it — reddit's own words, verbatim, because "sign-in failed" sends
the reader nowhere.

### 11.1 The protocol, and why it is the old one

`POST https://www.reddit.com/api/login/<user>` with `user`, `passwd`,
`api_type=json` — the legacy endpoint, which answers with an `errors` list and
sets the session cookie. The modern flow cannot be used: the saved sign-in
page is **341 KB of web components with a recaptcha in it**, 244 KB of that
inline CSS, and there is no form in it at all for a proxy to fill in.

Three things follow, and the second is the one that will bite:

- **`old.reddit.com/post/login` is tried as a second opinion**, not as a
  retry: it sets the same cookies by a different route and it answers where
  the API endpoint 403s.
- **Success is not what the login endpoint says, it is what old.reddit says.**
  `whoami` fetches the front page and reads the username out of the header,
  because a login endpoint can answer 200 and leave you logged out. The name
  it finds is what the home page then shows.
- **Two-factor cannot work this way** and the refusal says so by name, with
  the way round it: export a `cookies.txt` from a desktop browser and start
  the proxy with `--cookies`, which is also how the session survives a
  restart (`/li` writes the jar back to that file on success).

### 11.2 Where the credential goes

Nowhere but the request line and one method. It is a `GET` because the guest
can only GET, so it *is* in the request line — and that is the one place it
appears:

- **the access log redacts it** (`GET /li?<redacted>`), as does the gate's own
  label for the response;
- **it is never tokenised** — the link store is a file on disk and nothing
  from `/li` is minted into it;
- **it is never pushed onto the history stack**, so `Back` cannot replay it;
- **it is not in the page that comes back**, and `tests/proxytest.py` asserts
  all four of those against a known password.

What it does leave behind is os8088's own location bar, which holds the last
URL it fetched. That is the machine in front of the owner, on the owner's own
network, and it is the same exposure as typing the password into the box in
the first place.

---

## 12. What the merged browser changed here

The browser gained a toolbar, a Back stack, Save As, a shared scroll bar, a
resolved form action and — the one that matters most — **a link arena on the
fetch path**. Four consequences for this tool, and the first is the reason the
two halves have to be tested together at all.

**Every link on a proxied page used to be dead.** `brnet.inc` has its own
`br_claim`, and the link arena was added to the *file* path's copy alone, so
`[br_lnkseg]` was 0 on every page off the wire and `br_anchor` drew each
anchor as plain text — which is its correct behaviour on a machine too small
to hold the arena, and was here caused by a missing claim. Links worked
perfectly from a floppy and never over the network. Since **this proxy's whole
URL space is minted links**, that bug made it unusable and nothing in it could
have said so: the pages were legal, the gate passed, and the anchors were
drawn.

**The page chrome lost two of its four links.** Back and Reload are buttons
above the page now, with the greying done properly against a fact (is there
anywhere to go). Repeating them in the page cost an anchor each and a row of
text — 71 ms on the field machine — on every page served. `Home` and `Full`
have no button and stay.

**A form action may be relative.** `br_submit` composes the action and sends
it through `br_resolve`, the same resolver an href goes through, so `/f/<tok>`
is eleven bytes and resolves against the page it is on; before that it was
handed to `br_split`, which read `f` as the host. The default stays absolute
while it fits, because that is what also works on a browser built before this
change, and switches to relative rather than dropping the form when the
proxy's own address is too long — the failure this removes.

**Save As writes the server's bytes**, which for a proxied page means the
*reformatted* page. A long article can be paged through, saved to A:, and read
back off the floppy with no wire at all — which is most of what §8's "disk
cache" recommendation was reaching for, from the other end.

---

## 13. The window, and the Windows .exe

**The audience for this section is one person: a tester with an os8088 machine
and no Python.** `tools/os88proxygui.py` is the same proxy with a window on it,
and `os88proxy.exe` is that window as a single file — no installer, no pip, no
Python on the machine it is copied to.

**It is a FRONT END and not a fork.** It imports `os88proxy`, turns its form
into the same argv `parse_args` reads, and runs the same
`ThreadingHTTPServer` — SPEC.md §20.5.1's rule about `os88ui.inc`, one source
for two worlds, at the scale of a host tool. Two hooks make that possible and
they are the whole of the engine's side: `LOG_SINK` takes `say`'s lines, and
`PAGE_SINK` takes one record per page — *which site, how big it was, how big
it got*. Nothing else was added and no behaviour differs between the two.

**A setting is a row in `FIELDS` and nothing else.** That table carries the
flag, the label, the kind, the default, the choices and the one-line hint; the
form, the saved settings and the argv are all built from it, and
`tests/proxyguitest.py` asserts every key in it is an option the engine
actually has. A setting the GUI offers and the engine ignores cannot survive a
run of the gate.

### 13.1 What the window says, in the order a tester asks it

1. **What do I type on os8088?** The address, in 16-point type, with a Copy
   button — the LAN address is detected, not typed.
2. **Is it working?** *Sites visited* fills up: one line per page, the site's
   own URL and `170K -> 5.9K, 2 pages, 1.4s`. **Not the proxy path** — a
   tester watching `/l/6q4mzt3a` scroll past learns nothing, which is what
   the gate's third assertion is about.
3. **Why is that page not loading?** The same list, in red, with the status.

Everything else is a setting with a working default, and the two that are not
obvious say why in a sentence beside them. The technical log — the gate's
complaints and the engine's own notes — is behind a *Show technical detail*
box, off by default, except that a `GATE:` line always shows: that one means a
page reached os8088 in a shape the browser reads wrongly, and it is the line
worth phoning about.

### 13.2 Getting the .exe, and the two Windows prompts

**It is built on a Windows desk**, with `tools\build-os88proxy-exe.bat`.

There is **no CI for it, and that is a decision** rather than an omission. It
was `.github/workflows/os88proxy-exe.yml` once and that workflow is gone: an
unsigned `.exe` produced on a `windows-latest` runner with `contents: write`
and an unpinned third-party release action is a lot of trust to place in a
convenience, and the filter it ran under was not the narrow one it looked —
`tools/os88proxy.py` is touched by every merge of the browser work, so
ordinary integration pushes each spent a runner building a binary nobody had
asked for, and a stale gate then put a red cross on commits that had nothing
to do with the proxy.

The gates are what a push would have been protecting and they are not
Windows-specific, so the honest place for them is the desk: `python3
tests/proxyguitest.py` while editing the window, `--selfcheck` and
`tests/proxytest.py` while editing the engine.

`tools\build-os88proxy-exe.bat` runs `--selfcheck` **before** it freezes
anything, and that rule is the part worth keeping whoever builds it: an .exe
of a build that fails its own gates is worse than no .exe, because the tester
is the one who finds out. Run `tests/proxytest.py` and
`tests/proxyguitest.py` yourself first — the workflow used to do all three,
and nothing does them for you now.

`--self-report <file>` asks a frozen binary what it is. It writes JSON rather
than printing, because a `--noconsole` binary has no stdout when a console
starts it, and anything that captured its output would capture nothing and
pass.

Two things will happen on the tester's machine and both are worth warning them
about in advance:

- **SmartScreen.** The binary is unsigned, so the first run says *Windows
  protected your PC*. **More info → Run anyway.** Signing it needs a
  certificate this project does not have.
- **The firewall.** The first Start raises a prompt, because the proxy
  listens. **Allow it on private networks only** — the window says the same
  thing in its own log.

### 13.3 What is verified, and what is not

`tests/proxyguitest.py` drives the real Tk application with `root.update()`
rather than a mainloop — the form becomes the argv, Start listens, a fetched
site appears in the box as a SITE, and Start-after-Stop works (asserted by
starting again rather than by binding a bare socket, which fails on TIME_WAIT
from the test's own connection and would be a fact about the test).

The PyInstaller flags are verified by **building a one-file binary here and
running it**: `--paths tools --hidden-import htmsim` produces a bundle that
answers `{"frozen": true, "fold": "htmsim"}`, so the fold table travels and
the frozen app is not quietly falling back to the crude one. **That build was
a Linux binary** — a Windows executable cannot be built anywhere but on
Windows, so the .exe itself has never been run by me, and the workflow is
where it is both built and checked.

---

## 14. The first field run, and the two things it found

An os8088 machine in 86Box reached the proxy and fetched pages through it. Two
reports came back, and they were not the same kind of thing at all.

### 14.1 Wikipedia rendered as `(empty page)` — and the cause was one hyphen

**`en.wikipedia.org` puts feature flags on the `<html>` element**:

```html
<html class="client-js vector-feature-language-in-header-enabled
             vector-feature-language-in-main-menu-disabled ...">
```

§3.3's boilerplate matcher looks for `menu` as a hyphen-delimited token,
found `-menu-` in `…-in-main-menu-disabled`, and **dropped the `<html>`
element**. The whole document went with it: 470 KB of page in, a title and
`(empty page)` out — 173 bytes. From the front of the machine that is
indistinguishable from a site being down.

Three fixes, and only the first is about Wikipedia:

1. **A junk rule may never remove a structural element.** `html`, `body`,
   `main` and `article` are exempt on any evidence — dropping one is never
   right, whatever its class says.
2. **A class is not believed against the SIZE of what it holds.** A nav, a
   sidebar or a footer does not hold a third of a page's text; an element that
   does is content whose class name happens to contain a word, so a
   class-based drop over `JUNK_SHARE` (30%) is refused and logged. Tag-based
   drops (`<nav>`, `<footer>`) are the author *declaring* furniture and are
   not second-guessed.
3. **A heuristic may not produce nothing** (§14.3).

`tests/proxy/wikipedia-main.htm` is the capture, scrubbed, and `--selfcheck`
holds it to a floor of 6 KB of output with the article's own words in it. That
check was **confirmed to fail on the old code** — 362 bytes with the safety net
off — because a floor no broken build can trip is not a floor.

### 14.2 The sign-in died on the wire, and the fix was to stop being slow

The browser said `Connection refused` while the proxy's own log showed it
fetching `old.reddit.com/login/…`. Nothing was refused: **the guest's socket
gave up while the proxy was still working.** A sign-in was up to four upstream
round trips — POST the login API, fetch the front page to see who we are, POST
the form endpoint, fetch the front page again — and *two of those are 300 KB*.

So `whoami` asks `/api/me.json`, which is a few hundred bytes and names the
user directly; every sign-in request is bounded by `SIGNIN_TIMEOUT` (12 s);
and a JSON refusal from the login API is now **final** — a wrong password
cannot be made right by a second route, and trying costs os8088 another
round trip it is already waiting through.

What that does not settle is whether reddit's legacy endpoint will accept a
password at all from a datacentre IP, which §6.2 has said from the start is
unverified. The difference is that the answer now arrives in seconds and in
words, on the sign-in page, instead of as a dead socket.

### 14.3 A heuristic may not produce nothing

The general form of §14.1, and the reason it is worth stating separately: an
empty result now **retries itself unstripped** and says so on the page. If
that is empty too, the page says how much markup arrived and how much was
dropped — because "this page is all JavaScript" and "the proxy ate it" look
identical from the front of the machine, and only one of them is worth
reporting.

### 14.4 …and the field can now send the page back

`--save-dir` (a folder box in the window) writes, per page: what the server
sent (`.src.html`), what os8088 was handed (`.outN.html`), and the sizes,
timings and transform counters (`.json`). It was asked for from the field —
*"maybe add a way to save the retrieved pages"* — and it is what turned §14.1
from a screenshot into a one-line diagnosis. It is off by default: a tester
who is not chasing something should not be filling a folder.

---

## 15. The second field run: `Connection refused`, in half a second

Two reports, and the first one's cause is the sharpest bug in this tool so far
because it was invisible to every test **and** to every fixture.

### 15.1 The guest's `Host:` header carries NO PORT

`br_req` composes `Host: ` + `br_host`, and `br_split` has already taken the
port off into `br_port` — so an os8088 machine fetching `192.168.1.189:8088/in`
sends **`Host: 192.168.1.189`**. This proxy built its form actions from that
header:

```
   http://192.168.1.189/li          <- port 80. Nothing is there.
```

Press Sign in, and the browser connects to port 80, gets a reset, and says
`Connection refused` — **in half a second**, on a proxy that is running
perfectly on 8088. The timing is what identified it: a refusal that fast is
not a timeout, not TLS, not reddit; it is a closed port.

Everything else worked because **a form action is the only absolute URL this
proxy writes**. Links are rooted (`/l/6q4mzt3a`) and `br_resolve` puts the
page's own host *and port* back through `br_ubase`, which honours a
non-standard port. Only the one URL built from the header was wrong.

`selfbase` takes the **host** from the header and the **port from the server
it is running on**. And `tests/proxytest.py` now sends `Host:` without a port,
the way the guest does — the old test sent `host:port`, which is exactly why
it never caught this. A test that is kinder than the machine it stands in for
hides the bug it exists to find, and this file has now said that twice.

### 15.2 …and no request waits on the internet any more

The second report was that after a failed sign-in, *every* page was refused
until the proxy was restarted. Two shapes were built to reproduce it — a
handler that explodes with `sys.stderr` set to None, and a slow `/li` whose
client gives up mid-request — and **the proxy kept answering through both**,
so the accept loop is not what fails. That is written down as a negative
result rather than a fix.

What did change is the thing that could strand a socket at all: **`/li`
answers immediately and signs in on a thread behind the answer.** The guest is
never holding an open connection while this proxy talks to reddit, so a small
TCP stack cannot be left with a socket in a state a restart is needed to
clear. `/in` reports what the sign-in is doing — *signing in*, *signed in as
X*, or the reason it failed — so Reload is the whole of "did it work".

Three more things so that a next time leaves evidence rather than a
description:

- **A line per request in the window**, for the routes that are not page
  fetches. It answers the one question a screenshot cannot: did the proxy
  *hear* the click. Nothing in the log when Submit is pressed means the guest
  never connected, and that is a different bug in a different machine.
- **`Server.handle_error` never touches `sys.stderr`**, and the window
  installs a sink when there is none — a `--noconsole` binary has `sys.stderr
  = None`, and stdlib code that prints a traceback there raises *inside* the
  reporting of the first problem.
- **A watchdog and a crash log.** If the accept thread dies the window says so
  in red and goes back to `Stopped`, and anything fatal is appended to
  `crash.log` beside the settings. (`crash_log` was **called from two places
  and never written** until §19.7 — this claim was false for four commits.)

### 15.3 One link a line

The home page read `Reddit  sign in` — two links on one line, the first one
the word *Reddit* — and the field pressed the one that reads like the sign-in
and got `/rd`, which while logged out is reddit's login wall and renders as
almost nothing. **The guest's hit-test is not at fault**: `br_hitofs` maps the
clicked column through `br_lmap`, which the *painter* fills, so a click lands
on the link it is over. The label was at fault. It is `Sign in to Reddit` and
`Reddit front page` on separate lines now, with a `Sign out` once signed in.


---

## 16. Reddit without an account

`old.reddit.com` shows a **login wall** to a signed-out reader, and §11's
sign-in cannot get past the modern one: reddit's login page is a web component
with a captcha in it, so the legacy `/api/login` endpoint is the only thing
this proxy can drive, and from a home connection it now answers with HTML
rather than JSON — which is what the field saw, as
`Reddit did not answer with JSON`.

Importing a `cookies.txt` still works and is still the answer for *your own
subscriptions*. It is not an answer for a tester who does not know what a
cookies file is, so the default upstream changed instead:

> **`www.reddit.com`, signed out, is the default. Reading reddit needs no
> account at all.** `--reddit-site old` is the switch back.

### 16.1 The modern markup is EASIER to read, not harder

This is the part worth knowing before anybody "fixes" the parser back. Modern
reddit server-renders its posts as custom elements whose attributes are
exactly the fields this proxy wants:

```html
<shreddit-post permalink="/r/…/comments/…/" post-title="…" comment-count="611"
               subreddit-prefixed-name="r/mildlyinfuriating" score="5035"
               author="…" created-timestamp="2026-08-18T14:18:25.910000+0000"
               content-href="https://i.redd.it/…" id="t3_1vrqi5a">
<shreddit-comment author="…" depth="1" score="608" thingid="t1_p4f9kch"
                  created="…" permalink="/r/…/comment/…/">
```

Against old.reddit that is a strict improvement in three places: **no
class-name archaeology** (the old score lives in one of three `score` spans,
two of which are one off the truth), **no sentence to parse** for a comment
count, and **a reply's depth is a number** rather than a count of nested
`div.child`s. `Builder` keeps unknown elements and their attributes, so all of
it is ordinary tree work — the handler is *shorter* than the old one.

Two fields need a little work rather than a read. The body of a post or a
comment is matched on its **exact** id (`t3_x-post-rtjson-content`,
`t1_y-comment-rtjson-content`) and never on `slot="comment"`, because a
comment's children are inside it *and* its own body contains a div named
`-post-rtjson-content`; either looser test returns a parent quoting its own
replies, which is a plausible-looking wrong answer rather than a failure. And
old.reddit ships `4 hours ago` where modern ships the instant, so `_m_ago`
computes the phrase — deliberately coarse, because it is the old page's
wording and a listing is read rather than audited.

### 16.2 What is NOT in the document, and what is done about it

A modern page carries the first screenful and fetches the rest with
JavaScript. Three consequences, and each is a link rather than a pretence:

1. **The feed's next page.** `<faceplate-partial slot="load-after"
   src="/svc/shreddit/community-more-posts/best/?after=…">` is the cursor the
   desktop page uses when the reader reaches the bottom. The endpoint answers
   a *fragment of `<shreddit-post>`s*, which this same parser reads, so
   pagination costs one link and no new code: **More posts**.
2. **Collapsed reply chains.** `faceplate-partial.more-comments-partial`
   stands where the chain was cut, and its `startingDepth` is what puts the
   link under the reply it continues — so the walk visits comments **and**
   stubs in document order rather than listing the stubs at the end, where
   they would say nothing about what they continue.
3. **The rest of the thread.** 25 of 612 comments is normal, so the page says
   `[25 of 612 comments on this page]`. Saying nothing is what makes a reader
   conclude the proxy ate them.

### 16.3 Two traps, both already sprung

**`/rd/r/pics` became `/r/r/pics/`.** The home page's new bookmark is written
with the `r/` on it and the route already added one. It takes both forms now.

**`/svc/` is never moved to old.reddit.** `--reddit-site old` rewrites every
reddit host, and the cursor links above only exist on www — so the switch
turned every *More posts* into a 404 on a host that has never heard of the
path. The exception is in `upstream`, where the rewrite is.

### 16.4 What is not proven here

The captures are real and the parser is written against them, but **no machine
in this container can reach reddit** (the agent proxy answers 403 to
`CONNECT www.reddit.com`), so what is checked here is the *parse* and not the
*fetch*. The open question is whether reddit serves the same server-rendered
markup to this proxy's plain `urllib` client as it served Firefox. If it does
not, the window now says so in as many words — `reddit answered a modern page
with no posts or comments in the HTML (a login wall or a robot check?)` —
which is a different report from a page that rendered short.


---

## 17. The page is not there either: reddit's JavaScript check

§16 was right about the markup and wrong about what arrives. Asked by anything
that is not a browser, `www.reddit.com` answers **8 KB whose whole body is a
spinner**:

```html
<script>document.addEventListener("DOMContentLoaded",async function(){
  var e=document.forms[0], n=await(async e=>e+e)("b23a5eb64d4b8037");
  e.elements.namedItem("solution").value=n; e.requestSubmit()},{once:!0});</script>
<form hidden method="GET" action="/">
  <input type="hidden" name="solution"/><input type="hidden" name="js_challenge" value="1"/>
  <input type="hidden" name="token" value="7afd…"/><input type="hidden" name="jsc_orig_r" value=""/>
</form>
```

The shreddit parser is correct and was never going to run. So the ladder is
three rungs deep now, and the new one is the bottom:

> **0. `.json` — reddit's own public endpoint.** No account, no OAuth, no
> markup. It is a *tenth* the bytes of the page and carries *more* than the
> page does, because the reply chains the HTML collapses behind buttons are
> simply in it. `--reddit-api html` is the switch off.
>
> 1. the shreddit HTML of §16, for a machine where the JSON is refused and the
>    page is not — and `after_fetch` falls back to it by itself when a `.json`
>    request comes back as a page.
>
> 2. old.reddit's HTML, which is what a signed-in session still gets.

### 17.1 The check is answered, not worked around

Whatever rung is being asked for, the check can appear in front of it — so it
is answered, and the **cookie** it earns unlocks everything after it. The
transform is one line in the page (`e+e` here, the literal doubled), so
`_jsc_eval` does the handful of shapes it takes — the identity, a string
literal, concatenation, `split("").reverse().join("")`, `[...e].reverse()`,
`toUpperCase`, `toLowerCase`, `repeat(n)` — and **refuses anything else**, out
loud, naming the expression it could not read so the next capture can teach it.
A wrong answer is a refusal with extra steps.

Three things in it are load-bearing, and the first two were bugs the gate
caught before the field could:

- **The form is at the BOTTOM of the page**, under a 6 KB inline SVG of the
  reddit logo. The first version looked in the first 4,000 characters, missed
  it, and fell through to the "JSON answered as HTML" branch — which asked for
  the same page without `.json` and got the same challenge.
- **The action is not necessarily the page you asked for.** This capture's is
  `action="/"`, so answering it lands on the front page; the original URL is
  re-fetched once the cookie is in the jar, which is one extra request and
  only on a challenge.
- **The query rides along.** The page's own script copies
  `document.location.search` into the form before submitting, so
  `?limit=50&raw_json=1` has to be re-attached — it is the difference between
  the answer and the front page.

### 17.2 What the JSON rung is, in one paragraph

`/r/x/.json` and `/r/x/comments/id/slug/.json` — a suffix on the path a reader
would have typed, so **every link this proxy already mints keeps working** and
only the fetch changes. A listing is `Listing → children[] → {kind: "t3",
data: {title, permalink, num_comments, subreddit_name_prefixed, score, author,
created_utc}}`; a thread is `[post-listing, comment-listing]` with `t1`
children whose `replies` is a whole Listing again, so the depth is *where you
are in the walk* rather than a field to trust, and a `more` node is the
collapsed chain — linked at the depth it stands at, exactly as §16.2 links the
HTML's stub. Bodies come from `body_html`/`selftext_html` through the ordinary
emitter, unescaped when they look escaped rather than on the strength of
`raw_json=1`, and `data.after` is real pagination rather than the feed cursor
the HTML admits to.

### 17.3 What is proven, and what is still the field's question

The parse is proven off `tests/proxy/reddit-json-*.json`, and the check is
proven off `reddit-jscheck.htm`, which is the page the field was given. Both
**shapes** are asserted to be the same as the HTML rung's — a reader must not
be able to tell which rung answered.

What no test here can reach: whether reddit serves this proxy the JSON at all,
and whether one solved challenge is enough for a session.

**The schema is no longer a guess.** A signed-out save of
`www.reddit.com/r/vintagecomputing/.json` came back from the field and is the
listing fixture now — it confirmed every field the reconstruction had assumed,
renders through rung 0 unchanged, and is asserted field by field by
`check_json`, so a rename upstream fails here rather than showing as a listing
with no comment counts. It also settled the escaping question: that save
carries no `raw_json=1`, so its `selftext_html` really does arrive as
`&lt;div…`, and both forms are asserted to render as the same paragraph. The
comments fixture is still `mkjson.py`'s reconstruction; a real
`…/comments/…/.json` retires the last of it.


---

## 18. The front page, and four ways to draw a reply tree

Two things off the second field run: the front page rendered empty while a
subreddit and a thread rendered well, and a reply tree is hard to *follow* on
79 columns even when every character of it is right.

### 18.1 `/.json` is not `/r/x/.json`

A saved `www.reddit.com/.json` came back from the field and renders here
perfectly — 25 posts through rung 0, unchanged. So the parse was never the
problem and the fetch is: **the front page endpoint answers a browser and can
answer this proxy an empty Listing** (or an error object), where `/hot/.json`
is an ordinary listing endpoint and answers either way. Signed out, *hot* is
what the front page shows anyway, so asking for it changes what is *asked for*
and not what is *seen*, and `after_fetch` does it the moment `/.json` comes
back with no `t3` in it.

**Two diagnostics landed with it, because "empty" was the whole report.** A
JSON body that is an *error object* (`{"error": 429, …}`) now renders as
reddit's own words instead of nothing, and a JSON body this proxy cannot lay
out says so with its `kind` and its child count. Neither is a fix; both turn
the next report into a sentence.

**And `Full` skips the first-class handler now, which it did not.** That link
is the one promise this proxy makes that it will never be load-bearing
(BROWSER-PLAN §8.3.1), and a site handler is the biggest guess in the tool. On
a JSON body it shows the raw reply as `<pre>` — unreadable on a good day, and
exactly what a field report needs on a bad one.

### 18.2 `--reddit-thread`: quote, bar, rule, table

There is no right answer here, so it is a knob and the field picks. All four
are rendered through `tools/htmsim.py` — the browser's own layout model — so
what is below is what the guest draws, not an impression of it.

```
quote (default)                     table
ReadingGlassesMan 130 points        +---+--------------------------------------+
No doubt formatted for Solaris.     |   | ReadingGlassesMan 130 points 6 days   |
                                    |   | ago - No doubt formatted for Solaris. |
> random-brother 11 points 5 days   +---+--------------------------------------+
> I see what you did there. Every   | > | random-brother 11 points 5 days ago - |
morning was like Sunshine :)        |   | I see what you did there. Every…      |
                                    +---+--------------------------------------+
```

The table is the one that reads best and it is not close: the guest computes
the column widths, so the gutter is a real rule and **the wrap hangs under the
text instead of under the marker** — which is the actual defect in the other
three, visible in the `quote` sample above where the second line of a reply
starts at column 0. It costs a border row per comment and it is the dearest to
draw; measured on the field's own thread it is *fewer* display lines than
`quote` (225 against 279), because the header and the first line of the body
share a cell.

Two things about the table style are load-bearing. **A table is the ATOMIC
unit of layout on the guest** (BROWSER-PLAN §3.2), so it is emitted in chunks
of `TROWS` comments with the `<table>` and its `</table>` in the *same* block —
`paginate` splits between blocks, and a table whose close tag is on the next
page renders the rest of the page inside a box. And a cell is one text run, so
the header and the body are joined with a dash rather than a `<br>`, which a
cell would drop.

`--reddit-indent 0` still flattens any of them, and `check_styles` renders all
four on every `--selfcheck` — the shape assertions ask `thread_marks` which
layout is armed, because a gate written against `quote` alone fails a
perfectly good `table` and can only be believed on one setting.


---

## 19. The landing page, and what was taken out

Four things off the third field run, and one of them was the browser's.

### 19.1 Two links with a space between them are ONE underline

`231 comments  r/vintagecomputing` rendered as a single link with an
underscore in the middle. **The proxy was right and the browser was wrong**:
whitespace collapses to a pending space spent at the next ink character, which
for `</a> <a>` is *after* `D_LNK1`, so the space sits inside the second
anchor's span and `br_underline` rules through the gap. BROWSER-PLAN §2.4.1
has the fix (`br_wflush`) and `tests/brlink.py` is the gate — pixels, not
text, because the text is identical either way. A/B'd on a cycle-accurate
5150: 32px, an 8px gap, 32px with the fix; one 72px run without it.

### 19.2 The sign-in is gone, not hidden

§11's mechanism cannot work any more: reddit's legacy endpoint answers HTML,
the modern flow has a captcha, and — the part that decides it — **reading
reddit needs no account** (§16). So the form, `/in`, `/li`, `/out`, the login
call and `whoami` are removed rather than left as a door that always refuses.
`--cookies` stays and is now the only way to be signed in: a `cookies.txt`
exported from a desktop browser, which asks the guest for nothing and stores
no credential here.

### 19.3 An address box beside the search box

The landing page had one box and it searched. It has two now — **Address** and
**Search** — and that is one `<form>` with two text inputs rather than two
forms, because **the guest holds exactly one form per page** (`BR_FMAX` is
four text fields inside it). `/s` decides on what came back: an address wins,
because typing one is deliberate where an old search term is just still
sitting in the box, and `http://` is added rather than demanded.

The bookmark list starts with the reddit front page and `r/vintagecomputing`,
both through `/rd` rather than as URLs — a route knows which reddit to ask
(§16/§17) where a bookmark pins the host it was written with. The old
`old.reddit.com/r/vintagecomputing` default was exactly that mistake and
rendered as a login wall with three links on it.

### 19.4 A sort has no trailing slash, and `Saved` was a dead end

`/r/x/hot/` + `.json` is `/r/x/hot/.json`, which **404s**; reddit's endpoint is
`/r/x/hot.json`, while a listing really is `/r/x/.json`. The two are not the
same rule, so the sorts row mints its links without the trailing slash and
`upstream` simply appends. And `Saved` is `/user/me/saved/`, which answers
`{}` to anybody not signed in — a link that always leads nowhere is worse than
no link, so the categories row is Home, Popular, All.

### 19.5 The window has tabs

The settings grid had fifteen rows and four of them were reddit's, which had
already pushed the general ones off the bottom of a laptop screen — and there
are four or five more first-class sites to come. `FIELDS` carries a **tab name
per row** and the window builds a `ttk.Notebook` from it, so a site is a tab
and adding one is still adding rows to that one table.

### 19.6 …and the tab shipped a crash the gate could not see

The tab went in as an *optional* seventh element, two loops still unpacked six,
and the window drew perfectly and answered **Start** with `too many values to
unpack (expected 6)`. Three things were wrong, and the third is the one worth
keeping:

1. `FIELDS` is **normalised to seven-long rows at import** now, so a row can
   gain an eighth element without hunting for the loops that would break.
2. `argv_from(values)` is the form-to-command-line body as a **plain
   function**; `App.argv` is one line that hands it the widgets' values.
3. **`tests/proxyguitest.py` skipped WHOLE where there was no Tk** — which is
   this container, whose python is not the one `apt install python3-tk`
   installs for — so the file that existed to check the window checked nothing
   here, and the one path every tester presses first was the one nothing had
   run. The settings table and the argv it builds are ordinary data: that half
   (`check_table`) runs everywhere now and the *window* half skips, loudly.
   `tools/os88proxygui.py` imports Tk softly for the same reason; `main` still
   refuses without it.

Verified by putting the bug back — six-long rows — and watching `check_table`
name it: *FIELDS row 'port' is 6 long, not 7 - a loop that unpacks it will
crash on Start*.


---

## 20. Every decision here is about the URL we ASKED for

The front page came back empty a second time, and the saved `.json` beside it
had the whole answer in one line:

```
"url": "https://www.reddit.com/?solution=…&js_challenge=1&token=…&raw_json=1&limit=50",
"ctype": "text/html",  "src_chars": 557972,  "stats": {"main": "full page"}
```

The check was answered — that part worked — and what it landed on was
`www.reddit.com/`, **without the `.json`**, 557 KB of HTML front page. Reddit
answers `/.json` with a **redirect to `/`** and serves the challenge there, so
the `Fetched` carried `/` as its url; every path test in `after_fetch` read
that instead of the URL that had been requested, the "ask again for what we
wanted" test compared `/` against `/` and skipped itself, and the ladder's
front-page rung never fired because the path did not end in `.json` any more.

**`after_fetch` takes `asked` now** — the URL `load` requested — and reasons
about that and nothing else. Three consequences, and the middle one is new:

1. after the check is answered, the **originally asked-for URL** is fetched
   again, cookie in hand;
2. the check is answered **once per chain**. A second one is a refusal wearing
   the check's clothes, and solving it again just spends the retry budget on
   the same answer instead of dropping to the next rung;
3. the front-page rung fires on `text/html` as well as on an empty listing, and
   it is tried **before** the HTML rung, because the signed-out HTML front page
   carries no posts at all.

### 20.1 A page frame with no posts in it says so

The generic pipeline *can* read that 557 KB — it is menus, a search box, a Log
In link — and reading it is worse than refusing: the reader gets a page that
looks like reddit and contains none of it (`Skip to main content`, `Open
menu`, `Sign Up`, and the field's screenshot). So a modern page with no
`<shreddit-post>` and no `<shreddit-comment>` returns a **notice** naming the
reason. It is the one place §10's rule 3 is overridden, and `Full` still shows
the reduction, because §18.1 made `Full` skip the handler.

### 20.2 The gate replays the field's sequence

`check_challenge` now drives the whole thing against a stub that behaves the
way reddit did: `/.json` redirects to `/` and answers the challenge, the
answer sets a cookie, the re-ask returns the HTML shell, and `/hot.json`
finally answers a listing. It asserts the order — check answered, **the URL we
asked for** re-fetched, `/hot.json` tried, the listing returned — and it fails
on the old rule: pass `asked=None` and the first assertion goes, naming the
query that was lost.


---

## 21. The posts were in the page all along, inside a `<template>`

`/hot` came back as 597 KB of HTML and this proxy said *page frame, no posts*.
It was wrong: `grep` finds **three `<shreddit-post>`** in that file. They are
inside `<template for="s_24f05_0">`, which modern reddit renders its feed into
for hydration to clone — and `Builder` MUTES template content, along with
`<script>`, `<style>`, `<noscript>` and `<svg>`.

Muting a template is right for a browser with JavaScript, where the clone is
what you see and the template is a duplicate. **On a machine with no
JavaScript it is the opposite: the template is the only copy there is.** So
`build_tree(text, templates=True)` reads them, and `RedditSite` asks for that
— it looks for `shreddit-*` elements by tag, so anything else a template holds
cannot mislead it. The generic pipeline still skips them, because there the
risk of duplicated content is real and nothing has measured it.

Measured on the field's capture: **0 posts and 81 divs → 3 posts and 148
divs**, rendering as a listing with a `More posts` cursor instead of a notice.

Three, and not twenty-five, because reddit server-renders the first screenful
and fetches the rest — §16.2's `load-after` link is how a reader gets further.

### 21.1 The JSON endpoints are asked as a SCRIPT now

The same run says why the JSON rung never fires: `/hot.json` was answered with
`text/html` and a redirect to `/hot/`. This proxy asks with a **Chrome**
User-Agent and `Accept: text/html,…`, which is a fair description of what
content negotiation plus *this is a browser, send it the app* looks like from
the outside. So a `.json` URL now carries `Accept: application/json` and
`os88proxy/1.0 (a text-browser proxy for a 1981 PC)` — a descriptive agent is
what reddit's own API rules ask for, and `--reddit-ua` overrides it. `Site`
grew `headers_for(url, opts)` for it, because a site knows things about its own
endpoints that the fetcher cannot.

**Unproven here, again**: nothing in this container can reach reddit, so
whether that is the whole difference is the field's to say. It costs nothing
if it is not — the ladder below it is unchanged, and the template fix means
the HTML rung now answers with posts either way.


---

## 19.7 …and Quit crashed twice, in the code that reports crashes

The field pressed the X and got two exceptions in one dialog:

```
AttributeError: 'App' object has no attribute 'who'      <- in stop()
NameError: name 'crash_log' is not defined               <- reporting the above
```

Both are the same species as §19.6 and neither could be caught by importing
the module or by driving the window: **nothing runs on Quit until somebody
quits.** `self.who` was the *signed in as …* label, removed with the sign-in
in §19.2, and `stop()` kept configuring it. `crash_log` was never written at
all — it was called from the serve thread and from the Tk exception hook, and
its whole reason to exist was to explain a crash, so the one path that had to
work raised **inside the reporting of the first failure**.

`crash_log` is now a real function (best effort by design: a reporter that can
itself fail is worse than none, so every failure in it is swallowed and the
caller's message still reaches the window), and the label is gone from
`stop()`.

**The gate is static, because these lines never execute here.** `check_names`
parses `tools/os88proxy*.py` with `ast` and reports two things: a global name
that is used and never defined, and a `self.X` that a class reads and never
assigns. It is deliberately small — module globals against the module's own
names plus the builtins, class attributes against everything the class and its
**in-file** bases assign, and any class inheriting from outside the file
skipped whole, because a gate with a page of false alarms in it is one nobody
reads. Proven against both halves: put `crash_log` back the way it was and it
says *uses `crash_log`, which is never defined*; put `self.who.configure()`
back and it says *reads `self.who`, which App never assigns*.
