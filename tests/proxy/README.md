# Fixtures for the reformatting proxy (docs/plans/completed/PROXY-PLAN.md)

Input pages for `python3 tools/os88proxy.py --selfcheck`, which renders each
one and checks the result against the browser's own limits. Nothing here ships
and nothing here is built by `make`.

`tests/htm/*.htm` are run through the same gate: those are pages the browser
already renders, so the proxy must not break one.

## `modern.htm`

**Synthetic, and every element in it is there because it breaks something.**
Written rather than captured so that it can carry all of them at once:

- `<section>`/`<article>`/`<header>`/`<footer>`/`<main>` — the guest **ignores**
  every one, so their paragraph breaks are lost unless the proxy converts them;
- a `role="presentation"` layout table wrapping the article and a sidebar;
- a `display:flex` row of three `.card` divs — what §4's div-to-table
  transform exists for;
- a `<dl>`, a real data table, an `<ol>`, a `<blockquote>`, a `<pre>` line
  120 characters long (the guest does not wrap `<pre>` at all);
- an `<img>` with `alt` and a `<figcaption>`;
- `&mdash;`, `&nbsp;`, `&ldquo;`, `&minus;`, `&micro;`, `&#937;` and a
  UTF-8 `°` — the fold to ASCII 0x20..0x7E;
- a nav, a cookie banner, a share widget, a `.advert` and a `.sidebar` — the
  boilerplate that must go, and must come back under `Full`;
- **two** forms, one `GET` with a hidden field and one `POST` with five text
  fields, a textarea and a 48-byte CSRF token: the guest holds ONE form, FOUR
  text fields and 95 bytes of hidden pairs;
- an `https://` link, a `mailto:`, a `#fragment` and an ad URL 120 characters
  long — none of which can be handed to `br_split` as it stands.

## `reddit-www-template.htm` — SYNTHETIC, and it is one property

The whole feed inside a `<template for="s_…">`, which is where modern reddit
renders it: hydration clones them out, so a machine with no JavaScript sees an
empty page unless the parser reads template content. The field's own front
page is 597 KB and every byte past this shape is chrome, so this is written
rather than captured — two posts and a `load-after` cursor. With the parser
skipping templates it renders as the no-posts notice, which is what
`reddit_shape` asserts against.

## `reddit-jscheck.htm` — REAL, and the reason the JSON rung exists

What www.reddit.com actually answers a plain HTTP client with: 8 KB whose
whole body is a spinner, a logo and a **hidden form** — a JavaScript check
(`js_challenge=1`). The shreddit parser below is correct and never gets to
run. `--selfcheck` answers it (`check_challenge`) and asserts three things:
the transform is read out of the script and computed, the reply carries the
hidden fields **and** the query the page was asked with, and the page we
actually wanted is re-fetched once the cookie is in the jar — this capture's
form posts to `/`, so without that the reader clicks a subreddit and gets the
front page.

The token and the script nonce are replaced; the literal the challenge asks us
to transform is kept, because that is the test.

## `reddit-json-listing.json` — a REAL save of reddit's own endpoint

`www.reddit.com/r/vintagecomputing/.json`, signed out, straight out of a
browser (192 KB, 25 posts). It is the **only** place in this tree where
reddit's schema is checked rather than assumed: `check_json` asserts every
field rung 0 reads is on every post, and that the `after` cursor is there —
a rename upstream would otherwise show as a listing that renders with no
comment counts and no groups, which is legal, plausible and wrong. It also
proves the escaping: this save has no `raw_json=1` on it, so its
`selftext_html` arrives as `&lt;div class="md"&gt;…`, and both forms have to
render as the same paragraph.

`scrub.py` grew a JSON branch for it — a Listing carries `modhash`, empty for
a signed-out reader and a live CSRF token for anybody else, so it is blanked
and then checked.

## `reddit-json-comments.json` — a REAL save of a thread

`…/r/vintagecomputing/comments/…/.json`, signed out (110 KB, 51 comments four
levels deep). Like the listing, it confirmed every field the reconstruction
had guessed — `t1` children whose `replies` is a whole Listing again — and
`check_json` now asserts that nesting as well as the fields.

## `reddit-json-more.json` — RECONSTRUCTED, and kept for ONE node type

What `mkjson.py` still builds. Neither field capture carries a **`more`**
node — a cut reply chain only appears in a thread big enough to have one — so
this reconstruction exists for that alone, and it is the only fixture whose
shape gate asks for a `[more replies]` link.

## `reddit-www-listing.htm`, `reddit-www-comments.htm` — REAL captures, MODERN

Pages saved from **www.reddit.com signed OUT**, which is the reddit this proxy
asks for by default (PROXY-PLAN.md §16): a subreddit listing (28 posts) and a
612-comment thread with 25 of them server-rendered. They are what the shreddit
parser is written against, and between them they carry the four things it has
to get right — the `<shreddit-post>` attribute set, `<shreddit-comment>` with
its `depth`, the collapsed `more-comments` stubs, and a feed's `load-after`
cursor, which is the only pagination the markup admits to.

The home feed was captured too and is **not** kept: it is the same code path as
the subreddit listing (the only difference is which `/svc/` endpoint the cursor
names), and these files are 700 KB apiece.

`scrub.py` grew two rules for them, and both were the refusal doing its job:
modern reddit hangs its tracking id off an ATTRIBUTE (`<shreddit-app loid=…>`)
rather than burying it in a script, and an ad post carries a signed beacon URL
per event — 38 KB of one.

## `reddit-front.htm`, `reddit-thread.htm` — REAL captures

Pages saved from a **signed-in** old.reddit session with Firefox: the front
page (170 KB, 25 posts) and a 191-comment thread (631 KB). They are what
`RedditSite` is written against, and between them they carry every shape it
has to handle — a gallery post, a self post, a promoted one, flair, a link
whose path is far past `BR_PATHMAX`, deleted comments, collapsed comments,
`load more comments`, and a comment tree nine levels deep.

**They have been through `tests/proxy/scrub.py`**, which is in the repo
because a page saved from a signed-in session carries the account with it: the
username in four places, a live **modhash** (reddit's CSRF token for that
session), a `loid` tracking id, the subscription list and the karma count. The
scrubber empties every `<script>` and `<style>` body — which is where the
first three live — replaces the account name, drops the personal lists, and
then **refuses to write the file** if any marker is still in it. Run it on any
new capture:

```sh
python3 tests/proxy/scrub.py ~/Downloads/saved.htm tests/proxy/reddit-front.htm myname
```

The `<script>` and `<style>` TAGS stay, empty, because "drops the content of a
`<script>`" is one of the things the fixture is checking.

## `reddit-listing.htm`, `reddit-comments.htm` — synthetic, and still useful

**Synthetic pages written to old.reddit.com's class names.** They were the
only fixtures until the captures above arrived, and they are kept because they
are now the only thing that exercises the **fallbacks**: they carry no
`data-permalink` and no `data-comments-count`, so the handler has to find the
comment link and its count in the DOM the way it would the day old.reddit
stops emitting an attribute. They are also 3 KB rather than 630 KB, which is
the difference between reading a fixture and grepping one.

They carry the traps the handler was written around: three `score` divs per
post (`likes`/`unvoted`/`dislikes`) inside a `midcol unvoted` container, an
`entry unvoted` div that also matches a careless class test, a `&bull;` score
on a promoted post, a nested comment thread three deep, a `[deleted]` comment
with no body, and a link whose path is 95 bytes long on its own.
