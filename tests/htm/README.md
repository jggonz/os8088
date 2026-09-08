# HTML fixtures for the browser (docs/plans/completed/BROWSER-PLAN.md)

Real-world pages the renderer is developed and checked against. Nothing here
ships and nothing here is built by `make`.

**`demo.htm` used to be the exception and is not any more.** It was the
fixture *and* the page in `MEDIA/` that a new user opens first, and it was
written for the first job — 5,696 bytes of pathological markup that never
says what the toolbar does. SPEC.md §71.12 gave the second job to
`apps/browser/browser.htm`, which is the program's manual written in the
program's own dialect. `demo.htm` stays here, unchanged, and stays on `make
browsertest`'s disk: `brtest`, `brclick`, `brreload` and `brtoolbar` open it
by name, and `tests/socktest` serves `GET /demo.htm` over a real socket. Do
not delete it, and do not put it back on a shipped floppy.

## `frogfind-de-ie5.htm`

FrogFind's front page, from `http://frogfind.de/` (the `.de` host of the same
service; `.com` was down when it was taken). 5,289 bytes.

**It is a browser's SAVE, not the wire bytes, and that is the first thing to
know about it.** Its own header says so — `<!-- saved from url=(0019)... -->`
and `<META content="MSHTML 5.00.2614.3500" name=GENERATOR>` — so what is in
this file is Internet Explorer 5's re-serialisation of its own parsed DOM:

- tag names upper-cased and attributes normalised (`align=middle`, which is
  not what a page author writes);
- **`<TBODY>` inserted** — almost certainly absent from the real response;
- attributes emitted **unquoted** (`bgColor=#ffffff`, `width=200`);
- `src` rewritten to a local `FrogFind!_files/` directory that is not here.

So it is good evidence of the page's **shape** — which tags, how tables are
used, what entities appear — and it is **not** the byte stream the parser will
actually be handed. Some of the differences make it harder (unquoted
attributes) and some make it tidier (explicit `TBODY`), which is why it cannot
substitute for a capture.

**A real capture is still owed**, taken off the wire rather than out of a
browser:

```sh
curl -s -A 'Mozilla/2.0 (compatible; os8088)' http://frogfind.de/ > frogfind-de.htm
curl -s http://frogfind.de/opensearch.xml > frogfind-opensearch.xml
curl -s 'http://frogfind.de/?q=os8088'     > frogfind-results.htm
```

The second of those is worth taking first: the page carries a `<LINK
rel=search type=application/opensearchdescription+xml>`, and an OpenSearch
descriptor contains the search URL **template** — so it names the query
parameter and the method **without needing a search to succeed**, which is
what the daily API quota was blocking.

docs/plans/completed/BROWSER-PLAN.md §1.1.2 is what this file settled and what it did not.
