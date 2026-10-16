# Browser roadmap after 1.4

This is the durable handoff for continuing the retro-web Browser work after
PR [#253](https://github.com/jggonz/os8088/pull/253). Keep the PR's final
description and review history as its record; this file carries the roadmap,
review outcomes and engineering lessons forward after the PR is merged.

## Baseline

- Browser 1.2b through 1.4 is the foundation: bounded HTTP redirects,
  sequential ASCII framesets, frame-relative links, basic GET/POST forms,
  text formatting, table spans/alignment, page titles and the existing
  fixed-memory text renderer.
- The PR branch includes the current `main` merge for EGA themes (#249). New
  work starts from PR head `238b00f1`, which contains upstream `main` at
  `3987ad05`; re-check `main` before any eventual PR or merge.
- HTTPS and JavaScript remain unsupported. The optional host proxy is not a
  dependency of the native Browser.
- The page remains bounded by the existing 20 KB document ceiling. Small
  pages must not pay for a larger fixed buffer; EMS/XMS remains optional work.

## Milestones

| Version | Scope | Status / direction |
|---|---|---|
| 1.2b | Quality of life: relative links, redirects, framesets, forms and navigation fixes | Integrated into PR #253 |
| 1.3 | HTML text formatting: emphasis, headings, lists, quotes, preformatted text and paragraph spacing | Integrated into PR #253; leading-space preservation is fixed and regression-tested on this branch |
| 1.4 | Table layout, alignment, additional form controls and simple POST | Integrated into PR #253; maintainer fixes and validation are recorded below |
| 1.5 | Navigation and usability: bookmarks, View Source, Windows/IE-era shortcuts, keyboard focus/selection and page/link context menus | Closed in the local 1.5 release commit; no push. Find remains deferred until it has a bounded UI and storage design |
| 1.6 | Memory-aware page storage and render-path audit | One commit after measurement and validation. Establish the conventional-memory path first; use optional EMS/XMS only through existing contracts |
| 1.6b | Separate Download Manager window and transfer lifecycle | One commit after validation. Add Save Link As / Save Image As and fixed-size disk writes without staging a whole response |
| 1.7 | Image handling, initially static GIF/JPEG, reusing PiXEL decoders where practical | After memory design; decode on demand with small reusable buffers |
| 1.8 | Compatibility and integration against early-Web pages; refine progressive rendering and interactions | Follow image and memory work |
| 1.9 | Full audit: code reuse, allocations, binary/RAM footprint and 8088 execution cost | Broad optimization after features stabilize |
| 2.0 | Consolidated stable Browser for early-Web use | Release milestone |

Modern CSS, JavaScript engines, Java applets and modern web frameworks remain
out of scope. The target is useful support for early-web pages on constrained
real machines, preserving the Browser's text-first behavior.

## PR #253 review record

The maintainer's review comment at
[issuecomment-6200134012](https://github.com/jggonz/os8088/pull/253#issuecomment-6200134012)
records the following validation: fast tier 44/44, `make small`, unchanged
kernel size baselines, floppy-image fsck, VGA and CGA QEMU checks, Browser
soak rows, `tests/brnav.py`, and one `make test-full` run. The `brrace` row
exceeded its wall limit both on this branch and at the merge base; it is a
test-budget issue on `main`, not a Browser regression. Vintage-hardware
validation remains a separate check.

The PR's roadmap comment is the source for the milestone scope above. The
maintainer replied that binary/text downloads and small, optionally loaded GIFs
were also desired, and suggested considering the 360 KB package budget when
deciding what ships where. For sequencing, keep 1.5 focused on navigation and
defer the standalone download manager to 1.6, where its streaming and storage
design can be settled together; retain memory work before inline images, and
consider a network-disk build if an optional feature materially grows the app.
The maintainer said they expected to merge PR #253 after review; as checked on
2026-10-15 it remains open and awaits the requested code-owner approval.

### 1.5 starting-point audit

Some roadmap labels already have partial implementations and should not be
re-created:

| Area | Already present | 1.5 gap |
|---|---|---|
| History | Eight URL-only entries, Back/Forward/Reload, and a History menu | Consider scroll restoration only if its memory and interaction cost is justified; URL history itself is not new work |
| Keyboard | Arrows, Page Up/Down, Home/End scroll; Enter submits a focused form | Make Tab cycle through address, supported controls and links with visible link focus; add a Browser-menu shortcuts reference and only bind shortcuts that are implemented |
| Selection / clipboard | Shared OS text clipboard and `W_ONRCLICK` point callback | Drag-select address or rendered page text; right click copies the current selection via `OSAPI_CLIP_PUT`, without adding a kernel popup API |
| Save/download | File > Save As writes the retained server response bytes | It only works after a page load and the response is bounded by the existing page source claim; there is no attachment/binary response path or download filename handling |
| Find | None | Search rendered document text without requiring a second full-page copy; define wrap and not-found behavior |
| View Source | Raw response bytes remain in the source claim until navigation | No source display mode; preserve byte lengths and prevent NUL/binary data from being treated as strings |
| Bookmarks | No persistent bookmark list | Reuse `apps/os88data.inc` / SYSTEM\\APPDATA conventions; bound count and storage, handle missing/read-only media, and avoid making the disk mandatory |

The download investigation should first decide whether 1.5 supports bounded
binary downloads within the existing 20 KB page claim or uses the existing
streaming file-write API to avoid that ceiling. A worker can receive only into
its existing source buffer today; adding disk writes must respect worker API
rules, file-dialog ownership, cancellation, disk-full failures, and low-memory
systems. Do not enlarge the fixed claim as a shortcut. Keep large-file and
EMS/XMS work in 1.6 unless the streaming path can be proven safe without a new
fixed allocation.

### Memory and download sequencing review (PRs #252 and #258)

PR [#252](https://github.com/jggonz/os8088/pull/252) merged EMS.DRV and
Video Player EMS/XMS buffering. EMS is an optional driver API (`DRVC_EMS`),
with 16 KB pages mapped into a held frame; it is storage, not a conventional
far pointer that remains valid independently of mapping. XMS is available on
286+; EMS can serve an 8088 when suitable hardware/driver is present. The
Video Player's ring and bank are workload-specific examples, not a generic
Browser page allocator. Its review also caught an important lifetime issue:
re-attaching EMS must not invalidate handles/pages still cached by an app.

PiXEL's reusable lessons are bounded streaming and lifetime-aware claims:
the UI task pumps file input into a small DMA-safe ring, a worker decodes rows,
and output/master/work claims are acquired only for the current operation and
released or made movable when possible. Its XMS use for undo is optional and
falls back to conventional memory; it does not provide a general page-store
abstraction that the Browser can simply include. The Video Player separately
demonstrates bounded ring reads, optional XMS/EMS read-ahead and a streaming
file path. Browser work should borrow these patterns, not the apps' whole
allocation schemes.

PR [#258](https://github.com/jggonz/os8088/pull/258) is documentation-only,
but its VRAM work is relevant to 1.6's memory/performance investigation. It
shows when direct rendering into video pages avoids a full RAM framebuffer and
how dirty-region/page tracking reduces redraw. That is a useful design lead
for keeping decoded graphics out of long-lived conventional-memory buffers.
It is not itself a general-purpose memory backend: VRAM cannot replace the
Browser's HTML/source, document or line-table storage.

The key constraint is the windowed API. `OSAPI_GFX_BLIT4` accepts a packed
4bpp source in the app's RAM and the kernel writes it to the live framebuffer;
windowed apps do not receive a framebuffer pointer. Direct VRAM access and
page selection belong to an exclusive `OSAPI_FSX_RUN` bracket, which suspends
the desktop and is not a rendering mode for an ordinary Browser window. The
1.6 audit should therefore test a bounded staging path: decode an image row or
tile into a small RAM buffer and blit it through the normal graphics API,
leaving the displayed pixels in VRAM instead of retaining a full decoded
bitmap. This could lower peak and retained RAM, while increasing decode work
when the window is exposed, scrolled or repainted. Measure that tradeoff and
verify redraw behavior before choosing it; VRAM contents alone are not a
durable per-window image cache.

**1.5 interaction contract:** keep this state in the Browser package:
a star at the toolbar's far right toggles a persistent remote bookmark (filled
means saved; clicking it again removes it), and the Bookmarks menu opens saved
URLs. The icon follows the address bar even while a request is pending or has
failed. Store bookmarks in `SYSTEM/APPDATA` on
the Browser's own volume, following SPEC.md §19.9; test disks must include that
folder. Each instance loads the file after its window is shown and rereads it
before adding a bookmark, so an older open instance cannot overwrite a
sibling's newer list. Menu labels point to each record's title field; its URL
stays separate for matching and navigation.
Local files have no star and Ctrl+D reports `Cannot Bookmark Local
Files`. Right-clicking a link offers Open Link and Copy Link; right-clicking
page content offers Save Page As and View Source. The OS
has a right-click callback and a system clipboard, but its popup tracker is
kernel-internal, so keep the Browser menu inside the package and do not change
the kernel ABI for it. Ctrl+O, Ctrl+S, Ctrl+L, Ctrl+U, Ctrl+D, Ctrl+R, F5 and
Alt+Left/Right and Backspace (when the location field is unfocused) map to
existing history actions. Find is deferred until it can be added without a
second page-sized search buffer or a heavyweight dialog.

Design the Browser's bounded content store in 1.6 with a conventional-memory
backend first and optional EMS/XMS backends only where their APIs and machine
tier permit. Extend the link menu with Save Link As / Save Image As and launch
the standalone Download Manager from those actions. It should receive and
write fixed-size chunks to disk, with progress/cancel, and must not stage the
entire download in RAM or EMS. Page storage and file download are different
sinks; they should share the bounded receive/pump discipline, not force binary
downloads to consume a full page allocation.

Use `OSAPI_FILE_DLG` for the destination name and the instance-scoped
`OSAPI_FILE_HERE` / `OSAPI_FILE_GOTO_QM` / `OSAPI_FILE_FIND` path to browse
local directories; do not mutate another app's current directory. Keep local
destination browsing distinct from a remote directory index, and do not
auto-open downloaded files. This preserves the 8088 path and avoids building
a temporary 20 KB-only downloader that must be replaced later.

The consolidated work is based on `upstream/main` at `996cf685` (Sticky Note
#266). Its tree includes the main work that landed after PR #253, along with
the EMS work (#252) and VRAM-game study (#258). The prior working history
merged this main at `27bb4172`; that tree is retained in the release commit.
Verify `upstream/main` again before publishing a later PR, since it can advance
during this work.

### 1.6 + 1.6b starting point

1.5 is closed in the local release commit. Keep 1.6 and 1.6b as separate
version commits in the same planned PR, followed by a separate 1.7 commit.
For 1.6b, keep the manager in its own window and make transfer ownership
explicit so it can continue while the page view navigates. The user-visible
actions are Save Page As from page context and Save Link As / Save Image As
from link context; downloaded files are not opened automatically.

Before implementation, update SPEC.md's Browser contract with the chosen
state machine and ownership boundaries. Reuse existing OS facilities: the
file dialog and per-instance directory APIs for local destination selection,
the socket worker model for non-blocking network I/O, and existing file-write
calls for bounded commits. Do not add a kernel/driver interface unless the
existing contracts prove insufficient. Treat local destination browsing and
remote server directory listings as separate features; this scope includes
only the local, bounded destination chooser. Preserve the 8088/conventional
memory path on all adapters and hardware tiers. EMS/XMS may be an optional
optimization, never a requirement.

The memory/render investigation starts from the existing post-1.4 sized page
claims in §71.14; first measure their peak and retained bytes, then identify a
change with a real reduction before changing the storage model. PR #258's
direct-VRAM and dirty-page techniques are a study lead for future image
staging, not an API the windowed Browser can call: normal windows draw through
the clipped graphics API. Any image-like scratch storage must remain bounded
and redrawable, with displayed pixels left in VRAM only as a transient result.

The 1.6 memory acceptance is a measured reduction in peak or retained memory
with unchanged page output and redraw behavior, on the baseline 8088 path.
For 1.6b, acceptance should cover cancel and supersede without a stale
worker writing into released memory; exact byte output for binary and text
transfers; redirects and HTTP failures; disk-full and destination-name
collisions; cancellation and close while a transfer is active; a full
directory and nested local destination; no whole-file RAM claim; and the
normal Browser navigation/bookmark/context suite. Test with no EMS/XMS first;
test optional stores only if the implementation uses them. The network guest
gate is QEMU with NE2000
(`make ethertest && make browsertest`); use the project's Ethernet harness
for deterministic bytes, then run the same UI flow interactively.

### Continuation handoff

- Working branch: `browser-1.5-1.7`, cut from the consolidated 1.5 release
  commit whose parent is `upstream/main` at `996cf685` (#266).
- The 1.5 release is one commit containing the branch's complete delta from
  that main, including the final menu-boundary and keyboard-selection fixes.
- The former working tip is preserved locally at
  `backup/browser-1.5-before-consolidation-aa4548b8`; the old remote branch
  was not pushed to or rewritten.
- Planned PR: one PR containing one commit for each of 1.5, 1.6, 1.6b and
  1.7. Do not publish or push until the user requests it. The upstream's
  documented squash-only merge means those version commits remain separate in
  the PR branch but become one commit on `main` after merge; retain this note
  when preparing the PR description.
- Before implementation, verify the 1.5 gates on this consolidated tree;
  then update SPEC.md before each 1.6/1.6b/1.7 interface or behavior change.
  The main worktree remains at `996cf685`; check for newer upstream commits
  before eventual publication.
- Validation already recorded after the main merge: `make`, fast tier 47/47,
  `make browsertest`, and `tests/brclick.py --adapter cga` passed. The Hercules
  `brclick` row passed before that merge and should be rerun in the final gate.

The maintainer added five Browser fix commits after the original 1.4 commit:

- `2271e601`: count table columns correctly; preserve alignment and link
  payloads; make every document walker consume inline cell metadata; fix the
  1bpp styled-text ink; strengthen table assertions and register `br14`.
- `012cedb2`: submit the focused form on Return; include only the clicked
  submit button; retain form records across frames; refuse unsupported select
  controls visibly; decode entities in `img alt`; repair the Save As test.
- `ea2b93e4`: release the graphics lock on every frameset exit; decide
  redirects before handing off or shrinking page buffers; follow empty-body
  redirects; convert POST 301/302/303 to GET while retaining POST for 307/308;
  resynchronize the location control after redirects.
- `644eed64`: re-claim full-size page buffers before fetching frameset
  children; restore the frameset URL/title/host after the final child; test
  large children and a fetch after frameset completion.
- `238b00f1`: preserve each link's frame owner; underline links that begin
  mid-line; add a fixture and assertion combining `colspan` with `align`.

These are useful patterns for future Browser changes: audit every walker when
adding document markers; treat register ownership and clobbers as contracts;
balance graphics locks and page claims on every exit; test state transitions
and controls in tables/frames; and make assertions discriminate against the
failure mode rather than merely find a nearby string.

## Open review notes

These were explicitly non-blocking in the maintainer's review. Decide each in
its own change rather than quietly broadening 1.5:

- Back/Reload after a POST uses GET because history stores URLs, not request
  bodies. This branch now states that in the status line; preserving/replaying
  a POST body would require a separate design and memory budget.
- The apparent ~235-byte BSS opportunity was audited. Offsets +543..+545 and
  +548..+779 provide 235 free bytes, but the live tail fields (`br_wtitle`,
  table scratch, and the frameset URL) occupy 237 bytes. The holes are separated
  by fixed live slots, and `br_url` begins at +780; compacting this tail would
  require lifetime-sensitive aliasing or moving the tested fixed URL/worker
  layout for at most two bytes. It is therefore not a safe or useful BSS
  reduction as part of this Browser follow-up. The assembler assertions remain
  in place. Revisit only alongside a measured memory redesign with a complete
  overlap-lifetime audit.
- Stale Browser version comments and the unused submit-label string were
  cleaned up on this branch. Continue removing dead labels only after proving
  both storage and lifetime redundant.
- Keep the two-cell scrollbar margin: the maintainer considers it a visual
  choice. `docs/INDEX.md`'s duplicate README entry is the generator's basename
  behavior and already passes `--check`; don't hand-edit it. The `brrace`
  timeout is an upstream test-budget item.

## Follow-up audit fixes on `browser-1.5-roadmap`

- `<pre>` now preserves literal leading spaces; `br14`, `htmsim --selfcheck`,
  and SPEC.md cover the behavior.
- Back/Reload after a POST response still issues GET, as the URL-only history
  design requires, but the Browser now reports that the POST body was not
  replayed. `brnav.py` covers the request and status message.
- Removed obsolete BSS aliases and their parser writes, the unused submit
  label, stale 1.2b wording, and corrected the encoder comment. No fixed worker
  offsets or package image/BSS layout were moved.
- Latest `upstream/main` was checked at `3987ad05` (EGA themes #249); it is
  already an ancestor of the 1.4 Browser head used for this work.

Host-side validation for these fixes: `os88index.py --check`, `checkdocs.py`,
`htmsim.py --selfcheck`, Python byte-compilation of the affected harnesses,
NASM assembly with the Browser recipe's include paths, and `git diff --check`
all pass. The full `make browsertest`/guest run remains to be repeated when a
Linux/QEMU test environment is available; this Windows session's WSL distro
cannot start because virtualization support was unavailable. An attempted
`python tools/os88test.py fast` also stops on Windows because the runner uses
`os.wait4`, which Python's Windows runtime does not provide; this is an
environment limitation, not a passing suite.
