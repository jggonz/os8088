# Browser page storage roadmap

Status: implementation in progress. Source storage grows incrementally in
conventional memory under the 20% reserve. Formatted-document and line-index
arenas now migrate to EMS after conventional growth reaches that reserve; the
EMS path is assembled and its no-board fallback passes, but real-board
acceptance is pending the 86Box run below. XMS and EMS-backed source storage
remain open. The formatted
stream can cross 64 KiB; layout walks normalized 48 KiB windows and stores
dword line spans, and the painter maps those spans on demand. QEMU's local-file
fixture formats a 78 KiB document, paints a line above 64 KiB, and follows a
link at that offset. Its high-offset text field accepts input and its select
cycles options after remapping the retained source; clicking the high-offset
submit button builds the expected query from dword field and source spans. The
3,004-line fixture still verifies line-arena growth. The 200 KiB conventional-
only fixture now verifies that a page stops at the last indexed line and
returns the unindexed formatted tail when reserve pressure prevents index
growth. The 196 KiB fixture is now the 86Box EMS acceptance case.
Network source storage starts small and grows in 4 KiB steps; the source parser
uses a 32-bit cursor and QEMU/NE2000 tests verify 160 KiB across 64 and 128 KiB
seams. A 96 KiB network page also reached its formatted sentinel and 1,100+
lines, but the driver reported many ring overruns even when the host fixture
was paced; this remains an unresolved network stress result, outside this
Browser-only change. Table composition is retained for small documents and
large-document tables currently fall back to ordinary flattened layout. High
link hit-testing is dword-safe. Form-control document positions and retained-
source cursors now use logical dwords and are exercised above 64 KiB.
Network/source spill and XMS fallback remain unimplemented, so the current EMS
support does not yet remove the source-size ceiling.
This roadmap governs the page storage refactor in `apps/browser/`; it does not
authorize changes to the kernel, OS APIs, or drivers.

## Fixed decisions — do not reopen

- **Implementation scope is the Browser only** (`apps/browser/`, plus its
  Browser-specific tests, fixtures, and documentation). Do not change kernel
  code, the OS API contract/implementation, or memory/network drivers.
- `OSAPI_FILE_READ` already supports normalized buffers larger than 64 KiB
  (SPEC.md §18.4.1). Do not redesign or extend the file API for this work.
- The blocker is inside the Browser: some source/document consumers still use
  16-bit offsets and direct segment accesses. Widen each persisted position
  and resolve it through a bounded span before raising its acceptance limit.
- The 96 KiB source and 196 KiB formatted sizes are minimum acceptance floors,
  allocated incrementally; neither is a preallocation size or final ceiling.
- The 20% conventional-memory reserve is a target, with EMS/XMS as optional
  Browser-owned backing stores when available. Keep the no-expansion Browser
  path working within its actual safe conventional capacity.

## Goal and acceptance limits

The Browser must accept at least 96 KiB of raw HTML source and at least 196 KiB
of formatted document data. Those are initial acceptance floors, not fixed
page caps or amounts reserved when the browser opens. Each arena grows only
when the live page needs more space. Before a conventional-memory growth, the
Browser rereads `OSAPI_MEM_AVAIL` and targets leaving at least
`ceil(load_start_free_KiB / 5)` conventional KiB available. This 20% is a
conventional-memory reserve target, not a limit on the logical page size: on a
small-memory machine, EMS/XMS lets the Browser keep accepting the page without
consuming that conventional headroom. If a larger page is still needed, the
Browser uses EMS when `EMS.DRV` answers and can grant pages and frame quarters.
XMS is a secondary store when available;
it is reached only through bounded UI-task `OSAPI_XMEM_COPY` calls. If all
eligible stores refuse growth, the page reports truncation/resource pressure
without treating the 20% target as a page-size ceiling.

The Browser remains functional with no EMS or XMS, including on an 8088 and
`kern_small`. Opening a short page must not reserve the 96/196 KiB floors.
Navigation and close return every conventional claim, EMS handle/quarter, and
XMS block owned by that page or instance.

The 96 KiB source and 196 KiB formatted sizes are required initial acceptance
floors, not stopping points. They must work over conventional extents when the
machine has room under the 20-percent reserve; the logical arena must then
continue growing on demand. If conventional growth would cross that reserve,
the Browser adds EMS pages and then XMS storage when those backends are
available, continuing at the same logical offset. The page's logical capacity
therefore depends on the stores actually available, not on a fixed 96/196 KiB
ceiling. If neither expansion store is available, conventional growth remains
bounded by live safe capacity and the Browser reports truncation there; the
20% target must never be confused with a fixed arena size.

## Existing limits and cause

The network receiver grows source storage in 4 KiB steps until the live
conventional-memory reserve refuses another claim; it no longer stops at 96
KiB. `tests/ethernet.py` verifies each 64 KiB seam and the tail, and checks
that the parser reaches the exact logical source length. The 160 KiB fixture
proves a third alias, with source claim growth from a 1 KiB start. The parser
maps its 32-bit cursor into bounded 48 KiB source windows, remapping only
between tokens; anchor href copies use the active mapped source segment.
`boundaryui100k.htm` keeps formatted output compact and asserts links survive
around the 16/32/64/72 KiB source positions. Local Open sizes its conventional
claim from the dialog's 32-bit file size; association launches use the
existing zero-capacity `OSAPI_FILE_READ` size probe before claiming.
`DOCWIDE.HTM` is a local 78,149-byte
source whose 78,055-byte formatted stream, 1,203 lines, tail paint, and final
link all cross the 64 KiB seam in QEMU.

The association route originally had only a fixed 96 KiB source claim because
`OSAPI_ARG_FILE` supplies a name and directory, not the file size. The Browser
now uses the existing zero-capacity read probe before claiming: it performs no
data transfer, returns the unpacked size in KiB, and keeps whole-file
`OSAPI_FILE_READ` semantics for the actual load. This avoids a new API and
prevents a large local page from failing merely because the association path
did not already know its size.

`br_doclen` has a high word, and `br_docensure` grows the formatted stream in
1 KiB steps up to the largest single conventional claim; `br_memroom` decides
the practical ceiling by preserving the load-start reserve. Table composition
uses a dword cursor. `LN_OFS`/`LN_END` are dword logical positions; layout
remaps its document window and the painter resolves line spans on demand. The
line-index claim itself grows incrementally to 62 KiB: its 16-bit byte
capacity cannot represent the rounded 64 KiB allocation, and 62 KiB safely
holds 6,348 ten-byte records. `LINARENA.HTM` grows that claim to 30 KiB. The
`LINARENA.HTM` fixture lays out 3,004 lines in the local-file QEMU guest test
and grows the line claim to 30 KiB, past the former 16 KiB cap. This removes a real
independent truncation point. `DOCWIDE.HTM` now exercises document positions
and painting across 64 KiB.
The table append path exposed and fixed a register-preservation defect:
`br_cput` had reused `DX` for the logical offset without restoring the table
rule's `DX` run count, so a rule could loop to the document ceiling. The
append now preserves the caller's counter, and the QEMU `PUBZONE.HTM` check
asserts a composed dword line span beyond `D_END`. High-offset link hit-testing
returns a dword, scans backward through normalized document spans, and is
covered by clicking `DOCWIDE.HTM`'s final link. Text/select document offsets,
select scan positions, and submit-button source attribute spans are logical
dwords; the QEMU fixture types into and cycles controls above 64 KiB and
submits the expected query. Large-document tables use flattened layout until
their compositor's internal cursors are widened.

## Implementation sequence

## Source map for the refactor

This is not a claim that a larger claim is enough. The current consumers of
the 16-bit positions must move with the storage:

| Pipeline | Current entry points and state | Required change |
|---|---|---|
| Network ingress | `br_go`, `br_take`, `br_nlen`, `br_netclaim` in `brnet.inc` | Incremental logical receive cursor and a UI wake when the current storage window fills. The current step bounds `NETV_RECV` to available claim space, then pauses before the next receive. |
| Local files and source retention | `br_load`, `br_saveas`, `br_saved`, `br_srclen`, `br_srcseg` | Read actual file size in bounded spans; use the same logical source cursor for HTTP and local files; Save As copies the full source in legal transfer chunks. |
| HTML parse and attributes | `br_parse`, `br_srcmap`, `br_act`, `br_tag`, `br_getattr`, `br_attreq`, `br_anchor`, `br_input`, `br_hidden`, `br_submit` | Source scanning uses a 32-bit logical cursor and bounded mapped windows; transient tag helpers keep short local spans, while persistent select cursors and submit attribute spans are dwords remapped on use. Anchor href copying follows the active window. |
| Formatted stream | `br_put`, `br_flushbuf`, `br_docensure`, `br_doclen`, `br_docseg` | Append through incremental conventional/EMS/XMS-backed arena operations; a flush may not assume the full stream is one 64 KiB segment. |
| Layout and line index | `br_layout`, `br_table`, `br_lineensure`, `LN_OFS`, `LN_END`, `br_nlines` | Layout now walks normalized windows and emits dword spans; table composition remains on the small-document path until its cursors are widened. |
| Paint, scroll, links and controls | `br_paint_line`, `br_hitofs`, `br_linkat`, `br_comp`, link/form/submission offsets | Painter, link hit-testing, editable/select fields, and submit-button spans resolve dword positions. The large-table compositor still needs migration; never retain a mapped segment across callbacks. |
| Ownership and cleanup | `br_claimpage`, `br_frameclaim`, `br_free_all`, `br_abort` | Own/release every extent, EMS handle and XMS block on navigation, frame replacement, cancellation and close; test the heap after each path. |

The shared invariant is: persisted positions are logical; `ES:SI`/`ES:DI`
exist only for the currently resolved span. Source still resolves through one
conventional claim. Formatted document mapping can migrate the whole logical
arena to EMS and map four consecutive pages per window; line records migrate as
a four-page EMS handle and use staged `br_lread`/`br_lwrite` copies across
16 KiB seams. The 86Box hardware run must still verify mapping, growth-copy,
rendering, and release. `br_tcompose` remains conventional-only and large
tables flatten on EMS pages. Save As and source retention remain
conventional-only. These are remaining limits, not claims that all page
storage is already expansion-backed.

### 1. Logical arena addresses and conventional backend

Logical addressing is a Browser data model and does **not** require EMS. First
implement it over conventional claims alone: an offset such as 70 KiB remains
the same logical position whether the accessor resolves it through a second
conventional extent, an EMS frame page, or an XMS bounce copy. Storage backends
are selected independently and may be absent; EMS/XMS expand capacity but do
not define the arena or parser contract. The no-expansion path remains useful
within the live conventional budget and reports truncation when it cannot grow.

The shipped packages are references for those backend operations, not a reason
to couple the Browser's address model to a media format. Video Player's
`vp_eopen`/`vp_emap` shows probing, allocating, owning frame quarters, mapping
EMS pages, and releasing them. Its `vp_xopen`/`vp_xcopy` shows a file-backed
XMS store with bounded transfers. PiXEL's `pv_xout`/`pv_xcopy` shows an opaque
XMS token, chunked copies through conventional memory, refusal handling, and
freeing the block. Their media rings, playback scheduling, and undo semantics
do not fit the Browser's random-access page data, so reuse the contracts and
failure handling while keeping the Browser arena API independent.

Network source storage starts at 1 KiB and grows its conventional claim by
4 KiB when full. The worker bounds each `NETV_RECV` to the current 64 KiB bank,
pauses with `[br_fin] = 6`, and posts growth to the UI task. Bank `n` maps at
`base + n*1000h`; the claim remains contiguous and `br_take` never carries a
chunk across an alias boundary. The UI task regrows under `br_memroom`'s
20-percent reserve; if it cannot, the worker drains and drops the rest so the
socket closes. Save As preserves the 32-bit byte count. The parser advances a
32-bit logical cursor through normalized 48 KiB windows and remaps only
between tokens. Tests prove EOF at byte 163,840 and verify data across the 64
and 128 KiB seams. The network 96 KiB integration reaches the formatted end
but reports NE2000 ring overruns; that result is not an approved performance
or stability claim. The formatted document and interactive form positions now
cross 64 KiB locally; EMS/XMS backing remains open.

The formatted writer, layout, painter, scroll index, and link hit-testing now
use logical addresses. Next widen the large-document table compositor, audit
View Source and any remaining reader, then verify 196 KiB output and cleanup.
No pointer into a temporary window may survive a remap or callback.

Implement the logical arena over incremental conventional-memory extents
before adding EMS or XMS. `br_memroom` remains the
single reserve guard. Source receive must pause at its current extent, hand
growth to the UI task, and resume only after the UI task regrows or adds an
extent. This preserves the worker/UI ownership rule for `OSAPI_MEM_CLAIM` and
`OSAPI_MEM_REGROW`. Local-file reads use `OSAPI_FILE_READ`'s normalized,
multi-segment transfer contract and grow to the actual file length rather
than the old 32 KiB ceiling. `DOCWIDE.HTM` verifies the 78 KiB local source
and formatted document in a QEMU guest.

The source-cursor substage is verified at 96 KiB for byte retention and parser
EOF, and at 160 KiB for source seams; the 96 KiB NE2000 run still has a ring
overrun failure. The first 200 KiB local fixture exposed a more important
conventional-only failure: the source and formatted stream consumed the reserve
before the 10-byte line index could grow past 1 KiB (102 lines). The writer
kept appending formatted bytes even though layout could no longer index them,
so the apparent 202,750-byte “floor” was not a usable page and End could never
reach its tail. br_layout now stops at the last indexed line, marks the page
truncated, and shrinks the formatted claim to that line. tests/brcapacity.py
checks that conventional-only fallback and End behavior. It does not claim the
196 KiB floor is met: that requires expansion-backed page and line arenas.
Resize repaint, table composition above its small-document path, and full
cleanup remain open.
The local 78 KiB fixture now types into a
high-offset text field, cycles a high-offset select, and submits its form using
the retained high-offset source attributes. Short pages continue to allocate
only the storage they use.

The line-record access layer centralizes index addressing and copies each
record through Browser-owned staging. This removes direct line-record pointers
from paint, layout, blank-line, rule, and hit-test readers/writers; the
conventional backing and 62 KiB line-table ceiling are unchanged. An initial
staging allocation overlapped Browser state and corrupted the source-length
high word. The final allocation extends `BR_BSS` by exactly `LN_SIZE` bytes
after the last existing state byte. The rebuilt QEMU `brarena` and `brcapacity`
gates pass, including the 78 KiB paint/link case and the 200 KiB
conventional-only safe-truncation case. The fast host gate also passes after
rebuilding derived floppy images. The first EMS document/line-index slice is
now assembled; the actual 86Box hardware result is still required before
claiming its 196 KiB floor. EMS-backed source and XMS remain open.

### 2. EMS backing, using the shipped Video Player pattern

Reuse the `DRVC_EMS`/`EMSV_*` ABI, not private port access. The Browser probes
`EMSV_CAPS`, allocates a contiguous run with `EMSV_ALLOC`, and owns only the
frame quarters it successfully obtains with `EMSV_FRAME`. It maps pages with
`EMSV_MAP`; logical offsets select a handle page and an offset within its
16 KiB page. A mapped pointer is valid only until the next map. Store handles
and quarter ownership in instance state and release with `EMSV_UNFRAME` and
`EMSV_FREE`; retain `EMSV_GONE` as the kernel's teardown safety net.

The implementation follows `apps/video/video.asm`'s `vp_eopen`/`vp_emap`
allocation and mapping checks, but does not copy its circular playback ring:
the Browser needs random access for layout, scrolling, links, forms, and
repaint. The first vertical slice migrates the formatted document and line
index only. It owns the four EMS page-frame quarters, grows document storage
in four-page groups, copies old pages through two mapped quarters, and frees
the previous handle after a successful move. The line index is capped at its
existing 62 KiB logical limit and migrates into four pages only when
conventional growth is refused. Short pages do not allocate EMS. Source,
network receive, Save As, and XMS fallback are still conventional-only. The
source ceiling and network worker pause/remap path therefore remain follow-up
work.

Current no-board regression: `make browsertest`, `python3
tests/brarena.py`, and `python3 tests/brcapacity.py` pass in WSL. The
conventional-only 200 KiB page remains safely truncated and the OS remains
responsive. The 196 KiB fixture did not create enough pressure: its measured
Browser RAM use was 271 KiB. Final document/line EMS acceptance uses one
numbered HTTP page served locally and opened with the same Browser build and
VM RAM in both runs. The current pressure fixture has 300 KiB of source,
about 300 KiB of formatted text, and roughly 40 KiB of line records, for an
estimated peak working set near 640 KiB while source and output coexist.
Without EMS, the Browser must report truncation; record its last complete
numbered row and confirm the OS remains responsive. With EMS enabled, that
same page must reach its final sentinel. The source must fit under the
conventional reserve for this slice to isolate formatted-document/line spill;
source spill remains a separate required stage. Close and reopen the Browser
and verify all EMS pages and frame quarters are returned. Do not claim the
end-to-end memory goal from the current document/line-index slice alone.

### 3. XMS fallback, using the PiXEL/Video Player contract

Use `OSAPI_XMEM_CAPS` and one `OSAPI_XMEM_ALLOC` block per arena or shared
page pool. Treat the returned DX:AX as an opaque token. Move only even chunks
of at most 32 KiB through `OSAPI_XMEM_COPY`; calls stay on the UI task, as
required by `apps/os88api.inc` §41.8. The 8088 path remains conventional/EMS;
XMS is selected by actual reported capacity, never CPU tier alone. Keep one
small conventional bounce window and do not allocate a block until a page
needs spill storage. Free the block on page replacement/close.

This follows PiXEL's `pv_xout`/`pv_xcopy` ownership and bounded-copy pattern
and Video Player's single-block subdivision rule. It does not copy PiXEL's
undo/master storage semantics into the Browser.

Acceptance: an XMS-only spill path reads back data byte-for-byte across the
32 KiB copy boundary, passes the large-page Browser checks, and frees its
block. A refused allocation/copy leaves the conventional reserve untouched
and reports the page limit.

### 4. Live memory accounting and diagnostics

At load start, record conventional total free KiB and allocatable alternate
capacity. Before each extent, reevaluate the current conventional total and
the Browser's unspent 20% reserve. Do not reserve unused floors or the full
reported EMS/XMS capacity. Keep per-arena used/capacity/backend counters in a
debug-only test surface so tests can prove growth is demand-driven and that
unused memory stays available to another package.

Acceptance: a short page leaves unused conventional and expansion memory
free; a large page consumes only the amount required; a second package can
claim the protected reserve; all stores return to their pre-load state after
navigation/close.

### 5. Keep the host proxy's budgets in sync

`tools/os88proxy.py` duplicates Browser budgets for portability and its
`--selfcheck` verifies those copies. It independently caps each response at
32,767 bytes and paginates around that limit
(`docs/plans/completed/PROXY-PLAN.md`). After the guest can consume larger
pages, raise the proxy page budget to the Browser's verified source limit and
its line/link/form budgets to the verified metadata limits. Keep a safe
fallback budget for older Browser builds if the proxy protocol can identify
them; otherwise document that the proxy and guest must be upgraded together.
Do not remove pagination until the guest proves it accepts the corresponding
response size.

Acceptance: proxy self-check reads the same limits as the assembled Browser;
large-page pagination and truncation behavior remain deterministic, with no
response cut inside a tag.

### 6. Regression, QEMU snapshots and emulator matrix

Use `tools/brmemory.py` fixtures served from the host at
`http://10.0.2.2:8092/` in QEMU with NE2000. The 96 KiB fixture verifies raw
source growth; the 196 KiB fixture verifies formatted output growth.
`boundary100k.htm` places visible markers at source offsets 16, 32, 64 and
96 KiB. Add boundary fixtures with links, forms, and table rows straddling each
16 KiB EMS/64 KiB conventional boundary. Assert the final sentinel, link/form
behavior, scroll-to-bottom, resize/repaint, Save As/source behavior where
supported, reserve, and complete cleanup. Run `tests/brnav.py` and the
registered Browser renderer/table/click/form/EMS rows after each storage
backend change. MartyPC's EMS profile is required for the board-backed gate;
QEMU's NE2000 is the conventional/XMS network gate.

For a local host build, `python tools/brmemory.py --out build/brmemory
--serve --port 8092` generates and serves six deterministic endpoints:
`/mem96k.htm`, `/mem196k.htm`, `/boundary100k.htm`, `/boundary160k.htm`,
`/boundaryui100k.htm`, and `/links400.htm`. The UI-boundary fixture places anchors, a table row and a
form control around the 16/32/64/72 KiB source positions while keeping
formatted output compact. The 100 and 160 KiB fixtures verify raw-source
growth across successive 64 KiB aliases. The Windows-native server already running for this checkout
serves these fixtures on port 8092;
keep QEMU's NE2000 enabled to reproduce the real network path.
`tests/ethernet.py --page build/brmemory/boundary100k.htm --snapshot
build/brmemory-results/baseline-snapshot.json` drives the same NE2000 path and
records the live Browser arena fields and heap claims. Keep snapshots outside
`build/brmemory/`: that directory is the HTTP server root and should expose
only the HTML fixtures.

### 86Box EMS acceptance run (manual, real page-frame mapping)

The final EMS check is a visible 86Box run because this repository's 86Box
Lo-tech board is the only available way to exercise the actual page-register
mapping outside the existing automated MartyPC EMS gate. Use an IBM PC/XT 5160
class machine with an 8088 at 4.77 MHz, 640 KiB conventional RAM, and two floppy
drives. Add **Lo-tech EMS Board, 2 MiB**, at I/O base `260h` with page frame
`E000h`; these are the board/driver defaults documented by SPEC.md §107.2 and
the Lo-tech board's recommended jumper configuration. Keep XMS disabled for
this run so the Browser's successful spill is unambiguously EMS. Use CGA or
VGA; display mode is not part of the EMS assertion.

Boot `build/emstest.img` in A:. It is the existing 360 KiB system disk with
`SYSTEM.CFG` bit 7 set, so `EMS.DRV` probes before the desktop starts. Put
`build/brmemems.img` (720 KiB, containing `BROWSER.O88` and the uncompressed
`MEM196K.HTM` fixture) in B:. The test is deliberately local and does not need
Ethernet, a proxy, or a host fixture server. Open and close `EMSTEST.O88` once
from A: as a board sanity check; then open `BROWSER.O88` from B:, choose
File > Open, and open `MEM196K.HTM`. Wait for Ready and press End. The last
visible rows must contain `END196KFLOOR`, the Browser must not report page
truncation, and the OS must remain responsive. This is the EMS-positive
assertion: the same file on the conventional-only QEMU fixture intentionally
truncates before that sentinel (`tests/brcapacity.py`). Close the Browser page
and app, reopen `EMSTEST.O88`, and verify its window says `Holding 3 pages, 4
quarters` (not `A later instance`); this checks normal page/handle release as
well as the kernel teardown fallback.

For the negative control, hard-reset with the Lo-tech EMS device disabled but
leave `build/emstest.img` unchanged. The same Browser page may truncate at the
safe conventional limit, but the OS must remain responsive and the Browser
must still close and reopen. Record both outcomes and the EMS driver's
loaded/not-found result. Do not use the no-board result to claim the 96/196 KiB
floors. The automated `tests/ems.py` contract gate remains the first check for
the driver's allocation, mapping, ownership, and teardown semantics; this
manual Browser run checks the Browser integration on the actual mapped frame.

Manual 86Box follow-up, 2026-10-18: with the NE2000/SLiRP network active and
the 2 MiB Lo-tech EMS board enabled, the Browser fetched the 307,200-byte
`ems-pressure.htm` fixture. The user reported that the page eventually opened
after a long `Reading` interval; the desktop briefly appeared stalled, then
recovered. After closing the Browser, `EMSTEST.O88` reported `Holding 3 pages,
4 quarters`, confirming the EMS pages/frame were available again after the
Browser closed. This is a positive EMS integration and release observation,
not a timing result or a measured conventional-memory snapshot. In the
subsequent no-EMS run, the same page displayed its prefix through
`ROW-000101`, which is consistent with partial-page truncation instead of
rejecting the response. The status-bar `Page truncated` message and continued
OS responsiveness still need explicit confirmation.

Artifact provenance correction: the 86Box run displayed no 1.5 bookmark,
selection, or context-menu features and identified itself as 1.4. The image in
`C:\\os8088\\src\\build\\brmemems.img` was built before the newer
`browser.o88` and came from the `main` worktree, whose Browser source is still
1.4. The 1.5 implementation is preserved in commit `10c8245c` and the active
`browser-1.5-1.7` worktree, which also contains the 1.6 storage work. The
current 720 KiB B: test image is
`C:\\os8088\\browser-1.5\\build\\browser15-memory-ems-root.img`; it contains
the Browser package at the volume root, the memory fixtures alongside it,
and `SYSTEM/APPDATA` as the bookmark store. The image passes
`os88disk.py --verify`. Keep this 720 KiB image in a 720 KiB-capable drive:
the user's first bookmark attempt used a
360 KiB drive. Instrumented MartyPC reproduced `FERR_IO` (`AX=2`) when the
full memory-fixture image crossed the 360 KiB drive's geometry; the same
bookmark add/remove test passed on a light 720 KiB image whose allocated
clusters remained inside that geometry. The 360 KiB `browser15-bookmark360.img`
also passes add/remove and verifies the live `SYSTEM/APPDATA/BROWSE.DAT` bytes.
This is a test-media geometry mismatch, not evidence that the bookmark store
is corrupt. Use the 720 KiB image only with a 720 KiB-capable drive, and keep
the 360 KiB image for a 360 KiB drive. The earlier
`browser-1.5-current.img` reference was stale and is superseded by these.
The current About card identifies the app as `os8088 Browser 1.6`. Treat the
earlier EMS and no-EMS observations as provisional until repeated with the
correct drive geometry.

86Box bookmark check, 2026-10-18: the user confirmed the bookmark test works
from the 360 KiB diskette. This validates the 360 KiB bookmark image on the
target emulator; it does not validate the 720 KiB EMS fixture or its drive
geometry.

Correct-tree QEMU regression, 2026-10-18: `tests/brnav.py` passed with the
NE2000 enabled, including the Browser 1.6 User-Agent, history, POST replay, redirects,
frameset child loads and Save As (completion message `Saved` and the response
bytes present on the writable test disk). `tests/brclick.py` passed keyboard,
selection/copy, bookmark add/remove and context-menu actions. `tests/brtable.py`
passed link hit-testing including a `../` href. `tests/brarena.py` passed the
3,004-line arena, 78 KiB formatted document, high-offset controls and table
composition fixtures. `tests/brcapacity.py` passed the no-EMS reserve fallback
for a 200 KiB source, retaining the full source while truncating formatted
output at the last indexed line. The QEMU path does not emulate the Lo-tech EMS
board, so it does not replace the 86Box acceptance run below.

No-EMS network saturation regression, 2026-10-18: `make brsourcecut` serves a
768 KiB response over QEMU's NE2000, with no EMS, and paces it to avoid ring
pressure. The Browser retained 373,760 source bytes, drained the remaining
response with zero packet drops/overruns, reached `BN_DONE`, laid out the
retained prefix, and set the status message to `Page truncated`. This catches
the former endless-load failure at the exact conventional source-growth
refusal path. The test also guards the 2 KiB minimum conventional allowance
for the initial document and line arenas, so the truncated prefix remains
renderable under the 20-percent reserve.

After local-file EMS passes, repeat separately over NE2000/HTTP with the
network driver active. That second run validates the worker's pause/remap/resume
path; it is not combined with the first because the local test isolates EMS
from network-ring pressure.

For each QEMU run, record `OSAPI_MEM_AVAIL`'s largest and total free
conventional KiB and the Browser's live claim sizes before load, after the
short page, after the boundary page, and after navigation. Include the Task
Manager RAM/HEAP view or an equivalent test dump so the check proves unused
capacity remained available rather than merely being reserved by the Browser.

Baseline captured against the current 32 KiB build using the expanded
`tests/ethernet.py --page ... --snapshot ...` path: QEMU's NE2000 delivered a
102,400-byte `boundary100k.htm`, `br_nstate` reached `BN_DONE`, but the Browser
reported `br_srclen = 32,767` and laid out only five lines. Its source and
document claims were each 32 KiB; `br_doclen` reached 32,709 bytes. This
confirms the silent source ceiling despite successful TCP and rendering. The
reproducible heap map plus arena counters are in
`build/brmemory-results/baseline-snapshot.json` in the root checkout; the test
emits it along with a screenshot when requested. The host's Windows-native
fixture server is on port 8092
(`http://10.0.2.2:8092/boundary100k.htm`).

A second QEMU/NE2000 baseline used `boundaryui100k.htm`, where an anchor,
table row and form are deliberately split by the 16/32/64/96 KiB source
boundaries. It also retained exactly 32,767 of 102,400 bytes and reported
2,730 display lines before the harness failed its full-source assertion. This
is the negative control for source-window work: a successful HTTP response
and `BN_DONE` do not prove that structures after the first 32 KiB reached the
parser. The result is in
`build/brmemory-results/baseline-ui-boundary.json` and its screenshot is
`build/brmemory-results/baseline-ui-boundary.png` in the root checkout.

After the incremental-growth change, the same fixture grew the source claim to
63 KiB and retained 64,511 bytes, then completed with truncation at the
one-segment ceiling. QEMU's heap snapshot shows a 63 KiB source claim and a
63 KiB document claim; the 100 KiB assertion still fails, as it must until the
logical arena and long-offset work lands. The run is recorded in
`build/brmemory-results/stage1-ui-boundary.json` and
`build/brmemory-results/stage1-ui-boundary.png`. The ordinary 442-byte
NE2000 browser test passes with `br_nstate = BN_DONE`, 14 lines and all 442
source bytes retained. The short-page snapshot confirms its source claim is
only 1 KiB after load (`build/brmemory-results/short-page.json`). These
behaviors can be checked with:

The 96 KiB acceptance fixture was also run on QEMU with the NE2000 driver
active. The host sent exactly 98,304 bytes; the Browser retained 64,511 bytes,
reported a 63 KiB source claim, and laid out 1,094 lines. The assertions for
98,304 retained bytes and a 96 KiB claim fail, as expected at this interim
revision. This records the 64 KiB segment-boundary failure directly against
the 96 KiB floor, rather than inferring it from the 100 KiB fixture.

The first raw-source alias change passed that fixture: QEMU/NE2000 reported
`br_srclen = 98,304`, a 96 KiB source claim and `BN_DONE`. It also read guest
memory on both sides of the 64 KiB boundary and at the end of bank 1. The
follow-up parser-window change now reaches logical source position 98,304 on
the 96 KiB fixture and both 100 KiB boundary fixtures. The compact UI fixture
retains two anchors across mapped source windows and completes with 24,768
formatted bytes. The ordinary 442-byte response still renders 14 lines. These
results validate source scanning and raw retention; the 63 KiB document ceiling
still truncates larger formatted pages, and local files above 64 KiB remain
untested in the guest.

```sh
python3 tests/ethernet.py --page build/brmemory/boundaryui100k.htm \
  --expect-source-bytes 64511 --expect-source-capacity-kb 63 \
  --snapshot build/brmemory-results/stage1-ui-boundary.json
python3 tests/ethernet.py --expect-source-bytes 442 \
  --expect-source-capacity-kb 1 \
  --snapshot build/brmemory-results/short-page.json
```

## Explicit non-goals

Do not change kernel memory policy, EMS/XMS drivers, public OS APIs, or generic
system allocation behavior. Image decoding is a later Browser milestone; this
roadmap makes the page storage capable of hosting those resources but adds no
image decoder. Do not claim a backend works until its acceptance gate passes.
