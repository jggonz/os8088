# Browser 1.6c — classic HTML and incremental parsing

## Base and current status

Implementation starts from the clean fork checkout `browser-1.6b-download-manager`
at `2083632a` (`Browser 1.6b: complete native Download Manager`). Work is on
`browser-1.6c-classic-html`; the older `C:\os8088\src` checkout is Browser 1.4
and is not the implementation base. The About and User-Agent strings are
advanced to 1.6c for this release.

The Browser already has one source claim, a formatted document arena with an
EMS fallback, and a separate line arena with an EMS fallback. The network
worker currently stores the complete response body in the source claim, then
the parser starts at offset zero after EOF. `br_parse` clears its semantic
state on entry and maps source windows with `br_srcmap`; it cannot currently
resume after a receive block. Formatted-output exhaustion sets `br_docfull`,
then the worker asks the UI to grow and replays the whole source. This plan
replaces that replay for network pages with resumable parser state while
retaining the single source copy required for View Source and Save As.

No new allocator or EMS manager will be introduced. The parser uses the
existing source offsets, document writer, document EMS handle/frame and line
arena. It does not copy an EMS-backed page into a whole-page conventional
buffer. Source and document failures remain distinct and visible.

## Stages

| Stage | Work | Gate |
| --- | --- | --- |
| A | Confirm base, build, run Browser tests, record current failures and memory/image sizes. | Browser builds; baseline behavior/failures recorded. |
| B | Add a resumable tokenizer/semantic parser over the existing source claim. Keep layout and paint after EOF; preserve source for View Source/Save As. | **Partial; physical EMS run currently fails responsiveness.** Network parsing resumes from a logical cursor, preserves parser state, and waits for incomplete tags/entities. Long comments and SCRIPT/STYLE raw text keep bounded terminator state. Fixed 1-byte, 64-byte, 512-byte and deterministic irregular chunks have passed the split-token fixture. The raw source starts in conventional memory and now migrates to the existing EMS frame/handle path when the 20% reserve refuses conventional growth; later EMS growth uses 256 KiB groups, up to a 1 MiB logical source. The parser stages at most one existing 512-byte receive-buffer slice from EMS before processing it, so document EMS mappings cannot invalidate the active source cursor; network receives still map only the current EMS page. View Source/form lookups use the same logical source mapper; Save As streams from mapped source in cluster-aligned chunks. Without EMS, the conventional reserve cutoff remains a visible truncation. Latest user field report: with EMS enabled, opening the 307,200-byte `ems-pressure.htm` from the host-server link froze the OS without graphics corruption. The staged-source change therefore remains unvalidated for field stability. The separate 768 KiB `EMSGROW.HTM` sentinel test is still pending on the rebuilt Browser. |
| C | Add BASE URL/target, named anchors, correct relative URL normalization and frame-target selection using existing metadata. | **Partial.** `BASE HREF` is resolved into the existing link arena and applied to root links; the first BASE wins. Each child frame retains its fetched URL as the fallback base and separately tracks whether its own first `BASE HREF` has replaced it. Query-only and `//host/path` resolution are present. `NAME`/`ID` values are stored in a bounded 32-entry index as logical document offsets. Fragment-only links use a local scroll path; an external `page#fragment` has its fragment stripped from the HTTP path, then scrolls after layout. Form actions resolve against the `BASE HREF` of their owning root or child document, and the resolver restores the active page host/path afterward. QEMU tests click real submit controls and verify `GET /base/send?go=Send` for a root form and `GET /childbase/send?go=Send` for a form in a named child frame. The cookie redirect fixture returns `Location: /cookie-next.htm#cookie-done`; its second request omits the fragment and the viewport lands on the later named anchor. `BASE TARGET` is retained per root/child and inherited by links and forms; explicit link/form targets take precedence, and `FRAME NAME` values survive source reuse. A keyboard focus fix skips unrendered `FRAME SRC` records so Tab reaches visible child links. Child `_self`, implicit child targets and matching named targets now route through the saved frameset and override the selected child URL. A two-frame Ethernet fixture verifies that the selected named child receives `/target.htm` while the sibling remains part of the composition. Because output is flattened, the current implementation re-fetches sibling documents too; independent child history and a child-targeted POST remain unsupported. Root `_top` and direct-child `_parent` use the top-level fetch path. `_blank` is visibly refused. |
| D | Improve malformed classic HTML recovery, table spans/nesting limits and form control semantics within existing bounded tables. | **Partial.** Focused tests cover text/select/choice controls, submit/reset, GET/POST construction and form BASE resolution. `BUTTON` with omitted type or `type=submit` remains a bounded clickable submit span with clicked-only `name=value`. `INPUT TYPE=RESET` and `BUTTON TYPE=RESET` restore text, checkbox, radio and select defaults from retained source/state; `tests/ethernet.py --form-reset-fixture` mutates each control, exercises both reset types, checks restored values and confirms no second HTTP request. Text fields retain source offsets, choices one default byte, and selects one original-option offset per field (96 bytes at the current bound), without copying forms. The same fixture verifies `LABEL FOR` focuses a matching `id` even when the label precedes its control; label and button hit spans share the existing eight-record table, IDs are exact matches up to 15 bytes, and implicit wrapping labels are not implemented. The test exposed and fixed the select-cycle label span being lost while advancing its source cursor. Password fields reuse the bounded text record and mask rendered cells in the paint buffer while retaining values for POST. Their label displays `HTTP NOT SECURE`; password controls inside GET forms are refused to keep values out of URLs, toast text and history. `tests/ethernet.py --password-fixture` checks masking, retained value and the real POST body without printing it; `--password-get-fixture` verifies GET controls create no field record. Tables recover a missing `TR` wrapper by composing leading `TD`/`TH` markers as one implicit row; `tests/brtable.py` verifies both cells share a display line. Outside `PRE`, CR/LF/CRLF collapse as one HTML whitespace run even inside `P`; the one-byte response fixture covers a split CRLF. Named references include the HTML 3.2 Latin-1 set with bounded ASCII substitutions; all 58 accented names pass their case-sensitive one-byte fixture. H1-H6 now use the existing bold glyph pass and no longer borrow link underlining; the font remains the native 8x8 cell face because the line table and viewport assume fixed 8-pixel rows. Actual larger heading sizes need variable-height layout and scroll mapping, beyond this small rendering correction. Other malformed-nesting recovery remains outstanding. The same runtime test still fails its pre-existing synthetic table-link click and cannot reach the URL-normalization assertion: it leaves the Browser idle and `br_path` empty, so it provides no new evidence about dot-segment resolution. |
| E | Parse bounded HTTP response metadata; support status/content type/charset and cookies; add chunked decoding only if it fits the current HTTP/1.0 socket lifecycle. | **Partial.** One bounded session `name=value` cookie is captured incrementally from `Set-Cookie`, sent only to an exact case-insensitive host match, and scoped by its quoted or unquoted `Path` attribute or the response directory's default path. Same-host redirect and Path-exclusion fixtures pass; the quoted Path fixture confirms `/restricted` does not leak to `/cookie-next.htm`. Content-Type is retained incrementally as a 31-byte media-type token; absent type preserves legacy HTML behavior, `text/html` and `application/xhtml+xml` are parsed, and another declared type is drained without storing its body in the source claim. Common 204/304 empty responses and 400/403/404/500 status text are handled. The first `charset=utf-8` parameter is recognized across receive boundaries and overrides META; the 1-byte-chunk fixture with conflicting META completes with the HTTP choice. Other charset conversion remains limited to existing META/default behavior. Cookie Domain/Expires, multiple cookies and persistence are not implemented. Chunked framing remains deferred because the client requests HTTP/1.0 and supports close-delimited bodies; adding a framing state machine without an HTTP/1.1 use case would add unmeasured worker complexity. |
| F | Consolidate memory accounting, 8086 checks, no-EMS/EMS tests and release documentation. | Browser-specific build and behavior gates pass; remaining limitations and measured sizes are recorded. |

Every code change must update SPEC.md before the implementation that it
contracts. This plan tracks the full requested scope; it is not evidence that
an unimplemented stage is complete.

## Scope fences

This release does not implement image decoding or rendering, image maps,
CSS, JavaScript, plugins, TLS, download-manager behavior changes,
`INPUT TYPE=IMAGE`, file upload, or a new cache subsystem. The user has
separately authorized moving existing Download Manager presentation code into
an embedded lazy part to recover primary-segment capacity. It does not change
the OS or driver APIs. Cache, multipart upload and full nested-table semantics
may be deferred only with measured or architectural justification.

The combined Apps catalog floppy is outside this Browser release's scope.
Applications can be installed online through The Wire; Browser acceptance
uses the dedicated Browser test disks and Browser-specific runtime fixtures.
At the user's request, the existing Download Manager is being evaluated for
an embedded lazy code part solely to recover primary-segment capacity; its
download behavior, UI contract and worker are not being redesigned here.

## Baseline

- `make -B build/browser.bin`: passed; Browser image 40,415 bytes.
- `make browsertest`: passed; `brtest.img` and `brtest360.img` build.
- `tests/brtest.py`: passed.
- `tests/br14.py`: passed.
- `tests/brnav.py`: passed over QEMU/NE2000.
- `tests/brtable.py`: failed on the base: table-cell click did not resolve the
  link, and `../` was not normalized to `/pub/notes.txt`.
- During implementation, `tests/brtest.py` and `tests/br14.py` pass. `brnav`
  passes on 1.6c after updating its About/User-Agent assertions. A 7,263-byte
  HTTP fixture passed with 64- and 512-byte chunks; a 2,256-byte fixture
  passed with 1-byte chunks. Each observed parser progress while the response
  remained in `BN_BODY`; output retained `STREAMDONE` after a split HTML
  comment and `<script>` body, without rendering the script's fake paragraph.
  A fresh run with deterministic irregular 1..257-byte chunks also passed and
  reached `BN_DONE`; the test harness's parser-cursor and source-length reads
  are separate samples, so the diagnostic intentionally reports only the
  cursor and active network state. `tests/brcapacity.py` passed on the current
  build: the 200 KiB source retained a safe prefix, emitted 5,225 formatted
  bytes/102 indexed lines, reported truncation, and discarded the unindexed
  tail. `tests/brfetch.py` reached `BN_DONE` and rendered, but its Ctrl+D
  bookmark-write wait timed out after 360 guest seconds. `tests/brtable.py`
  still has its baseline table-click and relative-path failures. A later full
  `brnav.py` rerun did not complete: its mouse helper remained blocked on QMP
  after the Browser opened, so that run was stopped; a previous run before the
  latest parser edits passed. EMS behavior on the current 1.6c binary remains
  unverified in 86Box/MartyPC.
- Current build after anchor, cookie and form-base work: `browser.bin` 42,788
  bytes; compressed `browser.o88` 33,436 bytes; package BSS 12,244 bytes. The
  prior measured parser build was 41,429 / 32,400 / 11,311 bytes, so this
  cumulative work adds 1,359 image bytes, 1,036 compressed bytes and 933 BSS
  bytes. The immediately preceding active build was 42,546 / 33,407 / 12,064;
  this form-base/frame-base correction adds 242 image bytes, 29 compressed
  bytes and 180 BSS bytes. No resident-memory or 8088-throughput measurement
  has been made for 1.6c.
- Target-metadata build: `browser.bin` 43,332 bytes; compressed `browser.o88`
  33,804 bytes; package BSS 13,094 bytes. `make browsertest` builds both
  Browser test disks. Runtime QEMU validation was blocked because the harness
  detected the shared kernel image changing while resolving symbols. No EMS
  test was attempted; physical EMS validation remains with the user.
- Continuation verification after the cookie Path correction: a clean Browser
  rebuild produced a 45,651-byte image / 35,550-byte compressed package with
  13,496 bytes of BSS. `make browsertest` built the 1.44 MiB disk at 350/2847
  clusters and the 360 KiB Browser disk at 179/354. QEMU/NE2000 fixtures passed
  for cookie Path exclusion, same-host cookie redirect, per-frame `_self`,
  `_top` and named TARGET, named `BASE TARGET` in a two-frame composition,
  root and child `BASE HREF` form ACTION, and submit BUTTON name/value. These
  checks do not validate physical EMS; that remains the user's hardware test.
  Additional HTTP fixtures passed for HTTP `charset=utf-8` precedence over a
  conflicting META charset, draining a declared non-HTML response without
  parsing it, and empty 204/304 plus navigable HTML 404 responses. The host
  index check passed. Browser-specific build and Ethernet fixture results are
  tracked above. No EMS result is claimed.
- Quoted-cookie-Path follow-up: the cookie isolation fixture now sends
  `Path="/restricted"`; it and the same-host session-cookie redirect fixture
  pass. The resulting Browser image is 45,732 bytes, compressed package 35,621
  bytes and BSS 13,496 bytes. The quoted-path support adds no persistent BSS.
- Form-reset follow-up: the Browser image is 46,465 bytes, the compressed
  package is 36,070 bytes and package BSS is 13,600 bytes. The added select
  default table uses 96 BSS bytes (one source offset per bounded field). The
  reset Ethernet fixture passed after changing a text input, checkbox, radio
  selection and select, then invoking both reset-control types and checking
  that all defaults returned without a second HTTP request. Root form BASE HREF,
  `brtest.py`, `br14.py`, Python syntax compilation, `os88index.py --check` and
  `git diff --check` passed after the reset work. No EMS result is claimed.
- LABEL follow-up: the Browser image is 46,964 bytes, the compressed package
  is 36,357 bytes and BSS is 14,001 bytes. The bounded ID table and temporary
  `for` value add 401 BSS bytes. The updated reset Ethernet fixture also
  verifies a label preceding its text control sets that control's focus; reset
  activation, all four control defaults and no-submit behavior still pass.
  Browser build and dedicated floppy construction pass. The root form BASE
  submit fixture, general `brtest.py` and `br14.py` pass on this implementation;
  unclosed button/label hit spans initialize as zero-width, so stale records
  cannot activate after malformed markup. `git diff --check` and the host index
  and Python syntax checks pass. No EMS result is claimed.
- Table-recovery follow-up: `br_tfindrow` treats the first cell marker as an
  implicit row only when it is outside nested tables; explicit `TR` rows are
  unchanged. The PUBZONE fixture adds a two-cell omitted-`TR` case, and
  `tests/brtable.py` confirms both values compose on display line 16. Build
  output is 46,978 bytes, 36,370 compressed bytes and 14,001 bytes of BSS.
  `brtest.py`, `br14.py`, the dedicated disk builds, the host index check and
  `git diff --check` pass. `brtable.py` reaches the fixture and passes the new
  assertion, but retains its two pre-existing interaction failures (the
  synthetic click does not navigate, leaving `br_path` empty); no new claim is
  made for URL normalization. No EMS result is claimed.
- Text-normalization and entity follow-up: ordinary source CR/LF/CRLF remains
  collapsed whitespace, including inside `P`; the Ethernet fixture now sends a
  CRLF pair one byte at a time and checks for exactly one resulting space. The
  `PRE` fixture continues checking visible CRLF line breaks. The Latin-1 entity
  fixture covers upper/lower accented names, a fraction, and soft hyphen under
  a one-byte response. `br14.py`, `brtest.py`, the one-byte Ethernet fixture,
  Python syntax checks, `os88index.py --check`, and `git diff --check` pass.
  The verified package is 47,393 image bytes, 36,723 compressed bytes and
  14,002 bytes of BSS: 61,395 of the 61,440-byte package budget. This leaves
  45 bytes for further code or BSS until package-size work recovers room. No
  runtime EMS claim is made.

WSL runs from `/mnt/c` require `QMP_SOCK=/tmp/os88-browser-qmp.sock`; Unix
sockets cannot be created in the mounted Windows workspace. The test helpers
remove their temporary QEMU process and socket at exit.
- Follow-on entity regression: the one-byte HTTP fixture now also checks
  additional case-sensitive HTML references across Latin-1 letters and classic
  punctuation (`agrave`, `Atilde`, `Yacute`, `thorn`/`THORN`, `bull`, `hellip`,
  quote marks, `dagger`, `permil`, `iexcl`, `uuml`, and `euro`). The fixture
  passes together with the CRLF-in-`P` and HTTP-charset-precedence assertions.
  A proposed high-bit name-terminator table rewrite was rejected: the current
  table already stores one high-bit replacement byte per name, so the rewrite
  saved no data and increased code size by five bytes. It was reverted; the
  shipped 1.6c build remains 47,393 image bytes, 36,723 compressed bytes, and
  14,002 BSS bytes (45 bytes below the image+BSS package cap). No EMS result is
  claimed.

## Package-space preparation before the remaining 1.6c work

The user authorized reducing fixed Browser buffers and investigating lazy
parts before continuing the outstanding 1.6c features. This does not change
the existing conventional-memory/EMS allocation policy or its thresholds.

### Buffer changes completed

`brdownload_bss.inc` now reuses the 240-byte request buffer for response
header lines after the complete request has been sent, removing the separate
128-byte line allocation. The request capacity was reduced from 256 to 240
bytes after bounding the current worst-case request at 233 bytes (95-byte
path, 63-byte host and current headers), leaving six bytes of headroom before
the NUL-free request limit. The formatted display scratch is 59 bytes because
`br_dl_drawcut` emits at most 58 display characters plus NUL. The numeric
scratch and 12-cell `FONT_RUN` share 13 bytes; percent painting appends `%` to
the numeric string in place. Together these changes reduce Browser BSS by 228
bytes. The in-place percent update removes five image bytes. The current
package is 47,427 image bytes plus 13,780 BSS bytes = 61,207 of the 61,440-byte
package limit, leaving 233 bytes. `make -B build/browser.bin` and
`make browsertest` pass. `tests/brtest.py`, `tests/br14.py`, and
`tests/ethernet.py --stream-fixture --expect-incremental` pass on this build.
The attempted `tests/brnav.py` run did not complete because its QMP mouse
driver remained blocked; it was stopped and is not counted as passing.

The reduced buffers are Browser/package-local scratch only. Source-page,
formatted-document and line arenas remain governed by their existing dynamic
allocation logic, including the current EMS fallback. No EMS runtime result is
claimed for this build.

### Lazy-part candidates

### Download Manager painter checkpoint (2026-10-22)

This is a recovery note for the graphical fixes made before the user's
combined Browser/EMS field test. No commit contains this checkpoint yet; use
the named edits below to revert only the Download Manager presentation work.
Do not revert the Browser 1.6c parser, navigation, HTTP, form, or memory work
when backing out a visual change.

- `apps/browser/browser.asm` now declares one embedded `OP_LAZY | OP_COMP`
  part and retains the Download Manager entry points, transfer worker, window
  callbacks and completion/cancel lifecycle in the primary image.
- `apps/browser/brdownload_part.asm`, `brdownload_paint.inc`,
  `brdownload_const.inc` and `tools/brdlsyms.py` assemble/package the painter
  as that embedded part. To undo this extraction only, restore the prior
  `brdownload.inc` painter bodies and remove those four new files plus the
  painter part declaration/build dependency; keep all transfer-engine code.
- The lazy call facade explicitly clears `AL` after `op_fetch` before
  `op_seg`. `op_fetch` clobbers `AL`; without the clear the manager window
  failed to load even though the part had been fetched.
- The icon group is shifted two pixels right. The arrow/document tables live
  in the lazy part's code segment, so `br_dl_drawhrowsw` and
  `br_dl_setdocpos` read them with `CS` overrides. Reading those tables via
  `DS` had produced horizontal stripes and misplaced document pixels.
- `br_dl_fmt_word` now stores decimal remainders from right to left; the old
  order reversed digits (for example, 100 appeared as 001). The progress
  percentage uses the same bounded byte-progress calculation as the bar.
- `br_dl_recvseq` brackets 16-bit receive-counter writes. The painter retries
  unstable snapshots and retains the last percentage, avoiding a backward
  jump while the 8088 observes a counter rollover/update between byte writes.
- Progress clearing is confined to the progress region; ETA is laid out beside
  speed, with `br_dl_setetawidth` preserving `CX`. The previous register
  clobber placed ETA over its label. Compact window heights, 56x12 buttons,
  and close/cancel placement are in `brdownload.inc` and
  `brdownload_paint.inc`.
- `tests/brdownload.py --long-percent` sends 70,003 bytes, crosses the 64 KiB
  receive-counter rollover, samples the live percent and asserts it is
  bounded and monotonic. It also captures active and complete screens.

Verification on this checkpoint: `make -B browsertest` and
`tests/brdownload.py --content-length --long-percent --shots=build/dlpercent-final-visual`
passed on the correct 1.6c checkout. Observed animation frames 0..7, bar
geometry x=120..488 within content x=96..536, final percent 100, monotonically
increasing live samples, and exact 70,003-byte file contents. The inspected
captures are `build/dlpercent-final-visual/download-active.png` and
`download-complete.png`; they show the corrected centered icons, intact line,
speed/ETA, progress and terminal Exit state. The QEMU run validates ordinary
download UI behavior only; it does not validate EMS.

If a later user test finds a visual regression, first compare the active and
complete captures and inspect only the painter/layout changes listed above.
Revert those specific hunks (or restore the checkpoint's pre-painter copies
from Git history if available); do not restore the entire old Browser image.
This note is the exact change inventory for that selective rollback.

### Current optional-command extraction and field-test setup (2026-10-22)

The small, self-contained selection-copy command is now in the same embedded
lazy code part as the Download Manager painter. `br_copyselection` and its
private `br_copyemit` moved from `browser.asm` to `brcopyselection.inc`; the
resident `br_copyselection` facade fetches the existing part and far-calls
entry 3. It is only entered from foreground UI commands. The worker, OS-owned
callbacks, selection tracking, context-menu ownership and bookmark-star paint
remain resident. The generated symbol include now accepts all primary `br_*`
symbols referenced by the part. This added 341 bytes to the part (3,295 bytes,
2,348 compressed) and saved 268 primary image bytes at the cost of five
additional BSS bytes; current build: 46,277 image bytes + 13,899 BSS bytes =
60,176 bytes, leaving 1,264 bytes under the 61,440-byte package image+BSS cap.

The other candidate areas remain present pending a safer measured seam. The
context popup's right-click, paint and dismiss flow directly calls navigation,
Save As, source-view, clipboard and download routines; moving that whole span
would require multiple cross-segment bridges from an OS callback. Bookmark
add/remove likewise shares retained menu pointers and wake callbacks. Their
gross source ranges are not guaranteed primary savings, so they are not being
counted or split into tiny parts without a call-graph/runtime gate. The current
extractable optional UI code is therefore grouped in one lazy part instead of
adding separate tiny parts for each command.

`make browsertestems` now builds `build/br16c-check.img`, a dedicated 1.44 MB
test floppy containing the exact Browser package and a linked 768 KiB visible
formatted-output fixture. The server already running at
`http://192.168.0.106:8091/` now also serves `/emsgrowth.htm` (786,432 bytes)
and the current package at `/BR16C.O88`; its index links to both. The detailed
combined RAM-only / EMS-on, lazy-command and download procedure is in
`docs/plans/BROWSER-1.6C-FIELD-TEST.md`. The image uses 1,681 of 2,847 data
clusters. `brtest.py`, `br14.py`, and the 70,003-byte `brdownload.py` rollover
test pass on this build. `brclick.py` still stalled in its QMP mouse-driving
phase before reaching the new selection-copy assertions, so selection-copy
lazy loading is explicitly left for the user's field test. No EMS validation
is claimed.

`OP_LAZY | OP_COMP` parts are stored inside the single `.O88` package and read
when fetched; they do not require a second installed file. Download Manager
presentation is the strongest current candidate: it is optional until the
user opens the manager or accepts a download destination. In the current map,
the drawing and live-paint code spans `br_dl_paint` (0x9DC1) through the end of
`br_dl_percent` (before `br_dl_timer` at 0xABAB), plus the live painter at
0xAC32..0xAD44: about 3,836 bytes before primary stubs. The generic parts
loader, measured in the existing Pixel build, occupies about 1.9 KiB including
its table; one Browser part uses a smaller table, with 102 bytes of standard
BSS plus the part-segment/vector state. That puts the first-order net primary
image saving near 1.8 KiB, subject to the assembled Browser map and package
gate. The network worker, filesystem completion callback and window callback
entry points must remain in the primary segment under `apps/os88parts.inc`'s
ownership rules. The worker cannot call `op_fetch`; therefore the UI must
fetch the part before a window is created, and primary-segment callbacks must
far-call it. Extraction and runtime testing are still pending.

The 1.6c parser, layout and ordinary table/frame paths are not good lazy
candidates: they run for common page loads and are intertwined with the
primary window and document state. The existing clipboard/bookmark/source-view
code in `br15.inc` is a second possible candidate. The current map places its
contiguous code/data from `br_star_empty` at `0x700A` through `br_settitle` at
`0x7A67`, a 2,653-byte primary-image span. It is optional work, but its public
callbacks, menu state, bookmark-star paint and dependencies on primary Browser
helpers mean the whole span is not a drop-in part. A smaller seam exists around
the context popup (about 1.1 KiB from `br_ctxshow` through `br_ctxcopylink`),
but its `WM_ONRCLICK` callback must stay primary and dispatch through a fetch
stub. Bookmark mutations similarly need primary menu/callback facades. These
are feasible candidates, but their actual net savings require a call-graph
pass and an assembled module; do not add their source-span sizes to the
guaranteed budget yet.

The other high-frequency Browser subsystems are poor lazy candidates. The
incremental parser, URL resolver, layout, tables, frames, forms and ordinary
HTTP fetch run on common page loads and share active document state. The download socket
worker and its wake/file-cleanup callbacks must remain in the primary segment
because they are OS-owned entry points and the worker cannot fetch a part.
Future image decoders and a JavaScript interpreter are natural lazy parts,
fetched only when a document needs them; their code size and API costs cannot
be estimated until those implementations exist. All parts remain embedded in
one `BROWSER.O88` package.

### Follow-up lazy audit: additional optional browser commands

The existing `br15.inc` code is a second viable lazy group, with explicit
primary-segment facades. The map separates the context popup at
`br_ctxshow` (`0x707F`) through `br_ctxcopylink` (`0x749E` onward), about
1.1 KiB of code. Its `WM_ONRCLICK` callback (`br_onrclk`) must remain resident
and dispatch to a lazy entry; menu/window ownership and the context state stay
in the primary BSS. The popup's actions can call lazy helpers for copy-link,
copy-selection, save-link and source-view. The selection coordinates and
link records themselves remain resident because normal drawing and mouse
tracking use them.

The same optional-command part could hold `br_sourcetoggle` (map
`0x7955..0x7A3E`, roughly 233 bytes), `br_copyselection` and its private
emitter (currently near the image start, roughly 0.3 KiB), plus the mutation
side of bookmark handling. The star bitmaps and `br_stardraw` cannot move:
the toolbar painter calls them during ordinary page paints. Likewise,
`br_bmmenuset` and the menu records remain resident because the OS retains
their primary-segment pointers; only the infrequent bookmark add/remove and
storage work may cross a lazy facade. `br_bmpost`/`br_bmservice` are window
wake entry points and must remain primary callbacks, with any optional work
called after the UI callback enters a loaded part. These boundaries need a
small call-graph and callback test before treating their source ranges as
recoverable bytes.

The current map measures roughly 2,385 bytes of gross candidate code across
those seams: 1,123 bytes for the popup, 654 for bookmark mutations, 297 for
source-view/title handling and 311 for selection copy. Those spans include
call boundaries that need to remain as resident facades, so the net saving
will be smaller. A reasonable planning estimate is about 1.2-2.0 KiB of
additional resident headroom after extraction, pending an assembled map and
runtime tests. Combining it with the Download Manager painter is more
efficient than creating many tiny parts.
The About handler is technically lazy-callable but too small to justify a
separate part. History construction, page navigation, forms, tables and the
parser remain resident because they participate in routine browsing. Future
image support should be split by decoder or by a small shared image frontend;
JPEG, GIF and PCX/BMP are not to be budgeted as one hypothetical module, and
JavaScript should likewise be a future part entered only for pages that need
it. Their sizes cannot be honestly estimated from today's code. The current
post-painter build measures 46,545 image bytes plus 13,894 BSS bytes, or
60,439 of 61,440 bytes, leaving 1,001 bytes in the primary segment. The
painter itself is 2,954 bytes (2,026 bytes compressed in `BROWSER.O88`).
Thus the optional-command group could raise primary headroom to roughly
2.2-3.0 KiB, not enough to promise that all future image and JavaScript code
will fit resident. Those features should be separate embedded lazy parts,
loaded only when a document needs them; their transient working memory is a
separate budget.

- Malformed inline-style recovery: the existing TORTURE fixture leaves `B`
  unclosed before the next heading. The layout pass now clears only the
  inline emphasis flags at paragraph and heading starts, so that malformed
  inline markup cannot style later blocks. `tests/br14.py` checks the next
  heading's line flags and passes; `brtest.py` also passes with identical blit
  and repaint output. The build is 47,406 image bytes, 36,737 compressed bytes
  and 14,002 BSS bytes, 61,408 of the 61,440-byte package ceiling. This leaves
  32 bytes; no EMS result is claimed.

- Password-control follow-up: `INPUT TYPE=PASSWORD` is retained as an existing
  bounded text field, but `br_build` substitutes `*` only in its paint buffer;
  the retained document value remains available to POST. The field is followed
  by `HTTP NOT SECURE`. Password controls in GET forms are refused before a
  field record is created, keeping submitted values out of URLs, toast text and
  history. The new Ethernet fixture clicks the field, checks its painted mask,
  appends a character, clicks Submit and verifies the exact POST body without
  printing it. `tests/ethernet.py --password-fixture` passes. `brtest.py`,
  `br14.py`, the one-byte charset/entity/CRLF fixture, Python syntax checking,
  the host index check and `git diff --check` pass. Final image: 47,432 bytes;
  package BSS: 14,008 bytes; combined: **61,440 bytes**, exactly the package
  ceiling. This leaves no unallocated package headroom. No EMS result is
  claimed; physical validation remains with the user.

- Bookmark-save URL isolation: the asynchronous bookmark service used to swap
  its queued URL through the live location buffer. It now compares and mutates
  records directly from `br_bmrequrl`, and snapshots/restores the active HTTP
  host and port around the disk transaction. The old 160-byte URL scratch slot
  became a 66-byte host/port snapshot, reducing package BSS by 94 bytes. A
  regression assertion in `tests/brclick.py` checks the host and resolved
  relative URL after saving. The latest build passes (`46,282` image bytes,
  `13,805` BSS bytes; 60,087 combined). `brtest.py` and `br14.py` pass. `brnav.py` could
  not complete in this WSL checkout because the emulator QMP socket on the
  mounted build directory is unsupported; a retry using `/tmp` opened the
  guest but stalled in its existing automation. The bookmark-specific click
  regression has not yet reached runtime here. No EMS result is claimed.

- Unmatched named `TARGET` fallback: when a link names a browsing context that
  is absent, the Browser now navigates in its top-level window. This is the
  usable fallback for a platform where the Browser does not create a separate
  popup/window; valid `_self`, `_parent`, `_top` and named-frame routes are
  unchanged. `tests/ethernet.py --target-fixture` now includes an unmatched
  target and verifies that it issues the expected top-level request. That
  Ethernet/QEMU fixture passes; a later rebuild removed the now-unused error
  string. This does not substitute for the user's physical EMS run.

- Field result from the user, conventional RAM only: the `emsgrowth.htm` page
  reached `row-000306`; after it opened, RAM that was no longer needed was
  released instead of remaining pinned as in earlier versions. The user
  considers the released memory a positive result and is proceeding to test
  with EMS. EMS behavior remains unverified until that test is reported.

- EMS-enabled field run exposed a responsiveness defect: loading the network
  `ems-pressure.htm` page left the OS unresponsive, without the earlier visible
  graphics corruption. The likely blocking path was final `br_measure` /
  `br_layout` running under the global graphics lock. The worker now marks the
  final-layout phase in the high bit of `br_parse_started`, drops the lock,
  yields every 4 KiB while scanning, and reacquires the lock only to publish
  and paint. Paint, click, key, drag, release and timer callbacks skip mutable
  page state during that phase; native minimize/close remain available. The
  existing `[br_fin] = 3` lifetime handshake remains active through layout, so
  close/navigation cannot free the page arenas until the worker stops using
  them. This is a responsiveness fix; it does not claim the EMS source spill
  passed. The visible form-control cap is now 22 (from 24) to keep the package
  under the 60 KiB image-plus-BSS ceiling; all existing form tests remain in
  scope. The package measures 47,781 image bytes + 13,654 BSS bytes = 61,435,
  leaving 5 bytes below the 61,440-byte ceiling. The 1.6c test disks are
  rebuilt as `build/br16c-check.img` and `build/br16c-check360.img`.
  A polarity mistake in the compact high-bit paint guards initially made
  normal paints return early; it was corrected before this disk rebuild.
  `brtest.py` now reports 23,143 ink pixels in its page-content capture and
  zero pixel differences between blit and repaint; `br14.py` also passes.
