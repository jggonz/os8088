# Browser roadmap: fixes and segment space before 1.7

Audit of the Browser as it stands on `main` at `e9d78a10` (2026-11-06), after
PR #306 (Browser 1.5-1.6c) and the maintainer's review commits. It covers two
things, in the order they should be done:

1. the items the #306 review **flagged and did not fix**, checked against the
   current code, plus defects found while reading it;
2. where the primary segment's room for 1.7 (inline images, SPEC.md 71;
   [BROWSER-IMAGES-PLAN.md](BROWSER-IMAGES-PLAN.md)) and later features should
   come from.

Every number here comes from `build/browser.map` of a plain `make
build/browser.o88` on that commit. Byte counts per routine are the distance
to the next top-level label, so they include the routine's local data.
**Projected savings are estimates.** Each one becomes a fact only when the
change is built and measured.

## Progress (branch `browser-1.7`, 2026-11-08)

| item | state | primary segment |
|---|---|---:|
| F1a/F1b: a drag across the 64 KiB seam stops at it; no phantom highlight | done; `brarena` drags across the seam (red on `e9d78a10`: end `0036`, anchor `FFF2`) | +52 |
| F2: downloads written under `NAME.$$$`, renamed when complete (SPEC.md 71.20.3) | done; `brdownload` seeds an old `DOWNLOAD.BIN`, `--cancel-active` must leave it (red on `e9d78a10`: gone) | +257 |
| F3: the percent label is one opaque run of four cells; the moving document erases only its exposed strip | done (part 0 only) | 0 |
| S1: the Download Manager's icons into part 0 | done | −655 |
| `.pl`, `.cfm`, `.xhtml` navigate; `br_dl_autosave` keeps DX | done; `brautosave` (fast, unicorn, 39 paths; red before: 4 of 39) | +21 |
| a page fetch that answers a file becomes a download (SPEC.md 71.20.4) | done; `brdownload-ctype` (red on `e9d78a10`: no Save dialog) | +146 |
| the server's `filename=` names that download | done; `brautosave` (11 header cases) and `brdownload-ctype` | +213 |
| a settled download stays settled: no CANCELREADY/FAIL loop after a cancel; Exit and the close box dismiss a failed one (SPEC.md 71.20.3) | done; `brdownload-cancel`, `-close`, `-failexit`, `-failclose` (all four red before: states `[10, 12]`, or the window stayed) | ~0 |
| **S2: the Download Manager into part 0** (SPEC.md 71.20.5) | done; part 0 is 9,228 bytes (6,901 packed), dropped by `br_dl_after` only when nothing needs it | **−5,084** |
| **F1 step 2: selection, Copy and Tab as dwords**, through `br_docmap` (SPEC.md 71.19.1); Tab walks from the focus (F1c); `br_hitofs`'s high word fixed | done; `brarena` (drag past the seam, Copy of both sides, Tab to the link past it), `brmega` leg 1b on kern_mega | +118 |
| section 3: a form field's value gathered before the document is mapped (`br_fsetval`, SPEC.md 71.18) | done; `brems` reads the field out of the EMS frame | −205 |
| **a page in EMS, end to end** (SPEC.md 71.18): MartyPC machine `os8088_5150_cga_lpt_ems` (cable + Lo-tech board + 256 KB); a select's parse read the frame across flushes; `br_flushbuf` wrote DX; a full receive was not kept inside its EMS page | done; `brems` (select via `br_fsetval`, View Source byte for byte, Close Source = first parse) | +63 |
| **S5: the URL store** (SPEC.md 71.23): the Back stack's URLs and the bookmark file in one movable claim; a transfer's buffer and Copy's text carved after it, so neither takes a claim of its own | done; `brbookmarks` (fast, unicorn: find, menu titles, URL copy, no store) | BSS −2,563 |
| `br_bmfindurl` compared record *n* with the query's tail past record *n−1*'s mismatch: only record 0 could be found, so Ctrl+D duplicated and the star could not remove the second | done; `brbookmarks` red with the old walk (3 of 17) | 0 |
| **S3: the rarely used UI into part 0** (SPEC.md 71.24): context menu, View/Close Source, form submit, bookmark commit; `br_dl_depth` and `[br_ctxopen]` added to the drop rule | done; `brclick` (menu, View Source), `brfetch` (bookmarks) | **−2,055** |
| View Source read `[br_srcseg]:SI` and wrote `[br_docseg]:DI` with 16-bit offsets, and sized the document as `[br_dockb] << 10` | done: through `br_srcptr` and `br_flushbuf` (SPEC.md 71.24); `brems` | (in S3) |
| **claims table** (SPEC.md 71.23): eight slots, 1.7's two included | done | |
| S4: EMS code into a part | **decided against** (SPEC.md 71.23): EMS runs when conventional memory is short, which is when a part read fails; its own part would be a ninth claim | 0 |
| A line table in EMS took the frame from the layout walk's document: Close Source of a 300 KB page laid out the heading alone, View Source lost ~90 lines (SPEC.md 71.18, `br_ldocback`) | done; `brems --pre --kb 300` red without it | +54 |
| A connection that FAILED presented its bytes as the whole page | done: `br_trunc`, `Page truncated` (SPEC.md 71.18) | (in the row above) |
| 768 KiB field pages (`emsgrowth`, `sourcepressure`) over the cable into EMS | no freeze; the source cap's bytes kept (512 KB and 768 KB) and marked truncated, View Source byte-exact | 0 |
| EMS arenas grew by a larger handle and a copy, each run contiguous: a 2 MB board refused 768 KiB at 512 KiB with most pages free; 1 MiB source and document caps; the line table capped at 6,348 lines | done: source, document and line records in chunked arenas (`br_ar_*`), the board the only ceiling (SPEC.md 71.18); `brems --net` (86Box) 768 KiB whole, View Source 13,563 lines | (below) |
| Growth over-allocated up to 50% (a 768 KiB source held 80 pages) | done: `Content-Length` read (`br_clbyte`) and the source grown once to it, conventional or EMS; arenas trimmed when the page stands still (`br_trimall`); smallest chunk an eighth of the free board. 48 of 48 pages now | (below) |
| A right click during layout opened a popup the layout's last paint covered, left open and invisible | done: `br_onrclk` ignores it as `br_onclick` does; a popup opened while the page arrived is closed on the worker's wake (SPEC.md 71.24) | (below) |
| EMS and a network card on one machine to test with | done: `vm/xt-wire-ems`, `make etheremstest`, os88box86api's `_ne1k`/`_ems`, `tests/brems.py --net` | 0 |
| S6: shared scratch URL buffers | not done (not needed: 12.6 KB free) | |

Image + BSS: 61,857 at the start, **53,650** now (42,284 + 11,366),
**11,374 free** of 65,024 (it was 3,167). The EMS chunks, Content-Length,
the trim and the popup fix cost 1,030 of the 12,404 S1-S5 had freed.

Images delivery 4.1 (image boxes, SPEC.md 71.25) then took 1,803: image +
BSS **55,463** (43,958 + 11,505), **9,561 free**.

Delivery 4.2's conventional tier (static GIF, SPEC.md 71.26) took 1,996:
image + BSS **57,459** (45,211 + 12,248), **7,565 free**. The decoder is
part 1, 7,712 bytes in the file and none resident.

Its EMS tier took 54: image + BSS **57,513** (45,227 + 12,286), **7,511
free**.

Delivery 4.3a (the disk tier, SPEC.md 71.27) took 433: image + BSS
**57,946** (45,618 + 12,328), **7,078 free**.

Delivery 4.3b (the kept pages and revalidation, SPEC.md 71.28) took 369:
image + BSS **58,315** (45,912 + 12,403), **6,709 free**.

Delivery 4.4 (animated GIF, SPEC.md 71.29) took 101: image + BSS
**58,416** (45,947 + 12,469), **6,608 free**. Part 1 is 25,097 bytes.

Delivery 4.4b (one fetch per animated GIF; animations held in the store
through the spills) took no resident byte. Part 1 is 25,738 bytes.

Delivery 4.5 (PNG and JPEG through PiXEL's parts, hosted by part 1, SPEC.md
71.30) took 81: image + BSS **58,497** (46,028 + 12,469), **6,527 free**.
Part 1 is 29,881 bytes; parts 2 and 3 are 12,018 and 19,101. BROWSER.O88 is
79,616 bytes on the disk.

**Release obligation (The Wire).** That is past `WIRE_FILEMAX`, 64,512 bytes,
and a plain Wire record over it refuses the whole catalog on every Wire in the
field. The Browser's live record is a plain one (no flags, `needs_wire` 7).
The release that first ships 1.7 publishes it as a version 2 archive
(`WF_ARC`, SPEC.md 92.13.1), as C64 and APPLE2 were when they crossed the
same bound, with `needs_wire` at 7 or above (SPEC.md 71.30).
`tools/os88wire.py` refuses the plain record, so the release stops on it
rather than shipping a broken catalog.

Delivery 4.6 (the older formats: PiXEL's EXTRAS and SIMPLE parts and an XBM
part, SPEC.md 71.31) took 28: image + BSS **58,525** (46,056 + 12,469),
**6,499 free**. Part 1 is 30,637 bytes; parts 4, 5 and 6 are 20,696, 10,550
and 628. BROWSER.O88 is 99,633 bytes on the disk; the 720 KB system disk is
712 of 713 clusters.

Delivery 4.7 (Save Image As and image maps, SPEC.md 71.32) took 962 (619 of
code, 343 of BSS): image + BSS **59,487** (46,675 + 12,812), **5,537 free**.
Part 0 is 12,592 bytes.

**Section 3, read and fixed:**

- **ES and a flush under a source pointer.** The parser was already safe:
  it copies each EMS source slice into `br_rxb` first. Three event-time
  form operations were not: Reset (text field), Reset (select) and a
  select's next option.
  - They mapped the source, called `br_docmap`, and only then read the
    value. The source and the document share one EMS frame, so with both in
    EMS the field got the document's bytes.
  - Fixed by one helper, `br_fsetval`.
  - **Verified in emulation since batch 4** by `brems`, on a MartyPC machine
    made for it: `os8088_5150_cga_lpt_ems`, the cable machine with a Lo-tech
    2 MB board and 256 KB of conventional memory. An 80 KB page fetched over
    the cable puts its source and its document both in EMS. Writing the row
    found three more shared-frame defects (SPEC.md 71.18).
- **`br_tablink` and Copy on kern_mega** read `[br_docseg]`. Both go
  through `br_docmap` now (F1 step 2).
- **`mov es` without `push es`.** A sweep of every Browser routine found
  ten that save it mid-routine, three bookmark helpers that leave ES = DS
  (harmless), and `br_cput`. `br_cput` only runs for tables, which are
  flattened on EMS and past 64 KiB documents (F1d). `br_dlx_onwake` now
  saves ES: `br_dlx_link` calls it and then copies with `stosb`.

Final passes (WSL Ubuntu, ext4 checkout, pinned nasm 3.02), batches 1 and 2:

- every `br*` row touched passed: `brarena`, `brclick`, `brdownload`,
  `-length`, `-auto`, `-ctype`, `brfetch`, `brrace`, `brmem` and `brnav`;
- `brdownload --cancel-active` passed;
- `test-full`: 85 passed, 0 failed.

Batch 3 (settled downloads, S2, F1 step 2, `br_fsetval`), same box, with
A/B legs against `e9d78a10`:

- `soak -k 'br*' -k ethernet`: 26 passed, 0 failed. This includes the four
  new download rows (`-cancel`, `-close`, `-failexit`, `-failclose`),
  `brarena`'s new legs, `brclick`, `br14` and `brnav`.
- `brmega`: passed under kern_mega (leg 1b: selection, Copy and Tab past the
  seam; leg 4: a download through part 0).
- Red on `e9d78a10`:
  - `--cancel-active`: the old `DOWNLOAD.BIN` was gone;
  - `--close-active`: states `[10, 12]`;
  - `--fail-exit`: the window stayed, and the state looped `[10, 12]`;
  - `--fail-close`: the window stayed;
  - `brarena`: the selection holds a low word (`FFF2`).
- `test-full`: 85 passed, 0 failed. `t_pins` keys part 0's fetch as `op:0`
  in `tests/pins.txt` now.
- **Environment trap, again:** the pinned nasm is
  `build/nasm3/nasm/nasm`, not `build/nasm3/`. A PATH one level short took
  Ubuntu's 3.01, and `make mega` stopped in `kernel/mega.inc`
  (`mega_xs is seven bytes a stub`). `browser.bin` and the part assemble
  byte for byte the same under both, so the `br*` results stand.

**Not verified:** no hardware or 86Box run, and no EMS run of the form-reset
fix (above).

**kern_mega:** `brmega` was red on `e9d78a10` (the Browser opened
DOCWIDE.HTM by association and stayed empty: `mega_bown` refused a
package's own conventional claim). The same defect was fixed on `main` in
the meantime (KERN-MEGA-PLAN 14.139's follow-up of 2026-11-06), so this
branch carries no kernel change of its own. `brmega` gained a leg 4: a
`.zip` downloaded on kern_mega, with the Download Manager's icons drawn from
the part's data through `br_dl_partdseg`'s selector, the file on B: byte for
byte and no `.$$$` left behind. **This verifies S1 under kern_mega.**

## 1. The budget, measured

| | bytes |
|---|---:|
| primary image (`browser.bin`) | 48,185 |
| primary BSS | 13,672 |
| **image + BSS** | **61,857** |
| `APP_MAX_SIZE` (SPEC.md 20.1.1) | 65,024 |
| **headroom** | **3,167** |
| Download Manager part 0, unpacked / packed | 3,342 / 2,335 |
| `BROWSER.O88` on disk | 34,367 |

**The 61,440 figure in BROWSER-1.6C-PLAN.md is stale.** That was
`APP_MAX_SIZE` before #277 raised it to 65,024 on 2026-10-20, and #277 was
already in the base the 1.6c measurements were taken on. Today's Browser is
417 bytes over the old figure and builds fine. Two rules from SPEC.md 20.1.1
still apply:

- The heap counts in KB, so image + BSS above 63 KB + 1 byte is a **64 KB
  claim**. Every KB saved here is also a KB less RAM per Browser instance.
- `MEM_OWNER_MAX` is **8 claims per owner** (SPEC.md 50). Section 5 counts
  the Browser's claims, because images will need more of them.

Where the primary segment goes (code + data, by source file):

| file | bytes |
|---|---:|
| `browser.asm` | 27,425 |
| `brdownload.inc` (resident half of the Download Manager) | 6,533 |
| `brnet.inc` | 6,139 |
| shared includes (`os88ui`, `os88line`, `os88dir`, parts standard, icons) | 4,934 |
| `br15.inc` (context menu, bookmarks, source view) | 3,042 |

## 2. Flagged by the #306 review, checked against the code

### F1. Tab-to-link and selection on pages over 64 KB or in EMS

**Status: still refused, and the refusal has holes.** Read in the code:

- `br_tablink` refuses on `br_docems` or `br_doclen_hi != 0`, then walks
  `[br_docseg]` with a 16-bit SI.
- Select All (`.selectpage`) refuses when `br_doclen_hi != 0`.
- A click starts a selection only while the hit's high word is 0 (`.hit`,
  `.selecttext`).
- **Defect (new, F1a): the drag does not apply the same test.** `br_selmove`
  calls `br_hitofs`, which returns DX:AX, keeps AX and drops DX. A selection
  started below the 64 KB seam and dragged past it gets an end offset that
  wraps around to a small number. The highlight and Copy then name the wrong
  span. `brcopyselection.inc`'s `.page` refuses only EMS, not
  `br_doclen_hi`, so Copy follows the wrapped span.
- **Defect (new, F1b): the highlight test ignores the high word.**
  `br_build`'s `.markstore` compares `lbase + cell` (low word only) with
  `br_selanch`/`br_selend` and with `br_tstart`/`br_tend`. On a page over
  64 KB, a selection in the first 64 KB is also painted on the matching cells
  64 KB further down.
- **Performance (new, F1c): Tab costs a walk of the document from byte 0 on
  every press.** `br_tablink` searches for link N by scanning every marker
  from the start. `br_tabnext` calls it once per hidden link it skips, which
  is O(links × document). On a 4.77 MHz 8088 a near-48 KB page is on the
  order of half a second per Tab.

**Fix, two steps:**

1. Make F1a and F1b correct first, a few bytes each. `br_selmove` stops (or
   clamps the end at `FFFFh`) when DX ≠ 0. `.markstore` sets no mark when
   `br_lbase_hi` plus the carry is non-zero, which is exactly the range the
   16-bit selection cannot name.
2. Then widen (the design choice the review left to the author):
   - `br_selanch`, `br_selend`, `br_seloldend`, `br_selcur`, `br_tstart` and
     `br_tend` become dwords. Watch the fixed BSS offsets that
     `tests/ethernet.py` and `tests/brfetch.py` read by number (`%error` at
     the end of `browser.asm`).
   - `br_tablink` and Copy walk through `br_docmap` a page at a time: the EMS
     and kern_mega contract, re-aimed only when the 16 KiB page changes.
   - Tab continues from the current link's `br_tend` instead of from byte 0.
     That fixes F1c on every page, small ones too.
   - About 150-250 bytes of code. Do it after section 4 has made room.

**Related limit not on the review's list (F1d):** `br_build`'s `.mtab`
flattens every table when the document is EMS-backed, ≥ 0xBFF0 bytes, or has
a high word. On those pages tables render as plain text. Keep it as a stated
limit. Widening the table walkers is its own project, after images.

### F2. A failed download over an existing file loses that file

**Status: half fixed on `main`.** `.failedend` leaves the target alone when
nothing was written. Once the first cluster is written, though, the old file
has already been replaced, and a later failure (`br_dl_partial_delete`)
deletes what is left. The user has neither file.

**Fix:** write to a temporary 8.3 name in the destination folder (for
example the chosen name with its extension replaced by `$$$`, or `~DL.TMP`).
On success, delete the target and then `OSAPI_FILE_RENAME` the temporary
file to it (SPEC.md slot `0x0115`). On failure or cancel, delete only the
temporary file.

- Both write paths need it: `OSAPI_FILE_WRITE_SEQ`, and the
  `OSAPI_FILE_APPEND` fallback for kernels without the sequential slot.
- `br_dl_partial_delete` already restores the destination folder around its
  delete, and the rename needs the same bracket.
- Interrupted between the delete and the rename, the user keeps the complete
  temporary file, which is recoverable. Today they keep nothing.

### F3. Download Manager writes pixels twice

**Status: confirmed, and slower than it needs to be.**

- `br_dl_percent_label` fills the cell white, then draws the text with
  `OSAPI_FONT_STR_XPARENT`: the erase-then-letter pair §6.1 forbids.
  `br_dl_progress_live` calls it on **every live tick, even when the
  percentage has not changed**.
  - **Fix:** keep the last percentage drawn and return when it is equal.
    Build a fixed-width right-aligned string (`"  7%"`) and draw it with
    `OSAPI_FONT_RUN`, which lays ground and glyph in one pass. Drop the fill,
    and drop the cell from `tests/textsites.txt` (line 87) if it was the last
    transparent use in the file.
- `br_dl_redraw_doc` fills the whole old document rectangle white, redraws
  the dashed path, then blits the document. Every pixel where the old and
  new positions overlap is written twice.
  - **Fix:** the WireBall rule (CLAUDE.md, "Damage history"). Keep the
    overlap, erase only the exposed strip, restore only the path under that
    strip, then blit.
  - Alternatively compose the 24×28 footprint plus its strip into one band
    and put it down in one `OSAPI_GFX_BLIT1` (the `retro-face` skill's
    shape).

### F4. Soak rows that failed identically before and after the review

`brarena`, `brmem`, `brclick`, `brfetch`, `brrace`, `lzship`.

- `brfetch`'s 360-second wait on the second Ctrl+D was fixed on `main` in
  #313. `lzship`'s cause was the 360 KB disk curation, not the Browser.
- `brarena`: the code side of the widening looks right. `br_input` stores
  `FLD_OFS` as a dword from `br_docpoint`, and `FLD_SCAN`/`FLD_FIRST` are
  stored and read as dwords. (`FLD_SCAN` is reused as a byte for
  radio/checkbox fields on purpose.) The open question is which control the
  test's `field0`/`field1` really are on the current fixture, and whether
  `br_select` stores its option offsets the same way.

**Result (2026-11-08, `e9d78a10`, WSL Ubuntu on ext4, pinned nasm 3.02):
all six pass.** `make` is green and its fast tier reads 71 passed, 0 failed.
`soak -k brarena -k brmem -k brclick -k brfetch -k brrace -k lzship`: 6
passed in 85 s.

- `brmem`: ok, 17.4 s
- `brarena`: ok, 27.3 s
- `brfetch`: ok, 27.3 s
- `brrace`: ok, 55.5 s
- `brclick`: ok, 65.5 s
- `lzship`: ok, 85.1 s (declared 30 s)

The failures the review reported have been fixed on `main` since then.
`brarena`'s widening is now covered by a passing row, so F4 is closed.

**Environment trap found on the way:** Ubuntu's packaged nasm is 3.01. It
encodes one instruction in `apps/dos/dos.asm`'s `dos_entry` a byte
differently, so `doswtail` reports 3 references outside a `WIN_ONLY` bracket
and `os8088.img` is never built. Every image-dependent row then fails with a
missing-file traceback. The pinned 3.02 (`tools/setup-nasm3.sh`'s
`PIN_TAG`, built into `build/nasm3/`) assembles it cleanly. Whether 3.01's
encoding exposes a real gap in the bracket is a question for the DOS
package, not this roadmap.

## 3. Defects to look for, by the classes #306 found

The review found stack imbalance, ES left on the wrong segment, and stores
through the wrong segment. `t_stkapps` now catches the first class
statically. A read for the other two should cover:

- every `mov es, …` in a routine that does not `push es` on entry
  (`brdownload.inc` had two);
- every routine that takes a source pointer (`br_getattr`, `br_srcptr`) and
  then calls an output routine that can flush (`br_fput` → `br_flushbuf` →
  `br_docensure`). When source and document share the EMS frame, the flush
  can remap the window under ES:SI. Codex's r3 analysis found this hazard;
  check whether `main`'s staging closes it for `br_input`'s `.initial` loop
  and `br_button`;
- `br_tablink` and Copy on **kern_mega**. They test `br_docems`, not
  `br_docfar`, and read `[br_docseg]` directly. That is a selector there
  (MC7). Confirm the selector reaches `br_doclen`, or route both through
  `br_docmap` (F1 step 2 does that anyway).

## 4. Segment space: where it should come from

The lesson from the parser: **a lazy part pays off for code that runs
rarely, runs in a few long calls, and can be loaded at a moment when the disk
is free.** A part is read from `BROWSER.O88`, so the first use costs a disk
read: about 400 ms per `int 13h` on the XT, and the package disk must still
be in the drive.

Two more rules:

- The worker may **call** a part the UI task has loaded and holds. PiXEL's
  decoders are the precedent (SPEC.md 106). The worker may never **load or
  drop** one, because that is file I/O (SPEC.md 20.6 rule 7).
- Under kern_mega, call through `op_cseg` and read data through `op_seg`.
  Part code cannot write its own segment there, so its variables stay in the
  primary BSS.

Ranked by bytes won per risk:

| # | candidate | resident now | est. net gain | why it qualifies | risk |
|---|---|---:|---:|---|---|
| S1 | **Download icons → part 0** | 660 | ~650 | only `brdownload_paint.inc` (already in part 0) reads them. `br_dl_blit1` passes ES:SI, so ES becomes the part's data segment | trivial |
| S2 | **Download transfer engine → part 0** | ~6,500 of `br_dl_*`'s 7,238 | **~5,500-6,000** | a transfer only exists while the manager window does (`br_dl_link` refuses with no window; Close during a transfer is a cancel request), and part 0 is held for that whole time. The worker's `br_dl_step` becomes a resident guard plus a far call: `cmp word [vec+2], 0` / `je ret` | medium: worker → part call; the cancel and close paths must drop the part strictly after the worker's last call |
| S3 | **Cold UI → part 0**: context menu, source view and save, bookmark service, form submit | ctx 1,123 + source ~990 + bm ~1,070 + submit ~1,020 | ~2,500-3,200 | each is a single user action, so a disk read on first use is acceptable. Put them in part 0, not a new part, so the claim count does not grow | medium: the context menu calls many resident helpers, so count the gates (4 bytes each plus call sites) before moving |
| S4 | **EMS promote/release/save → its own part, held for the instance when EMS.DRV answers** | 1,779 | ~1,500 | runs only on EMS machines, which have memory to spare. A machine without EMS never loads it. `br_docmap`/`br_srcems_map` stay resident, because they run per byte | medium: claim count (section 5); not callable from the worker |
| S5 | **URLs out of BSS** into one small heap claim: Back-stack URLs (`br_hist`, 8 × 160) and bookmark URLs (8 × 160 in `br_bmdata`) | 2,560 BSS | ~2,400 | read only when navigating or saving. The titles stay in the segment, because the menus point at them | low-medium: +1 claim |
| S6 | Share one 160-byte scratch URL among buffers that are never live together (`br_basescratch`, `br_frameoverrideurl`, `br_bmrequrl`, `br_dl_barbak`, …) | ~1,600 BSS | ~300-600 | each union needs a written argument that the lifetimes do not overlap | low, but easy to get wrong silently |

**Do not do:** move the HTML parser into a part. It runs on every page, so
the part is held anyway and saves no memory. Its 17 gates sit on the hottest
path in the program, and the 1.6c audit itself ranked it a poor candidate.
The working tree that tried it is preserved on
`backup/browser-1.7-codex-attempt`.

**Out of reach for now:** cookies (856) and the HTTP header path run on the
worker for every response. Moving them would need a part held from the first
fetch, which is the parser's problem again.

S1 to S5 together come to roughly **12-13 KB**, so headroom grows from 3.2 KB
to about 15-16 KB. That is enough for 1.7's resident image glue (layout
boxes, the fetch queue, the call into a decoder part) and for whatever comes
after it. Do S1 first: it is the smallest change and it shows whether the
part's data segment works under kern_mega.

## 5. Claims (MEM_OWNER_MAX = 8)

| claim | today | after S4/S5 | with images |
|---|---|---|---|
| source arena | ✓ | ✓ | ✓ |
| formatted document | ✓ | ✓ | ✓ |
| line table | ✓ | ✓ | ✓ |
| link arena | ✓ | ✓ | ✓ |
| part 0 (Download Manager + cold UI) | when used | when used | when used |
| Download Manager transfer buffer | during a transfer | during a transfer | during a transfer |
| URL store (S5) | — | ✓ | ✓ |
| EMS part (S4) | — | EMS machines | EMS machines |
| image decoder part | — | — | ✓ |
| image row/cache buffer | — | — | ✓ |

The image column reaches 10 when everything is live at once. Before 1.7:

- carve the transfer buffer and the URL store out of one claim, or out of
  part 0's claim (SPEC.md 20.12, "One claim, carved");
- decide whether the EMS part and the image decoder may never be resident
  together, and refuse the second with a stated reason if so.

Write the decided table into SPEC.md 71 before any code.

## 6. Order of work

Each step is one reviewable commit. A step is "done" when it is built,
measured with the map, and its named test rows have run.

1. **Baseline**: a working test environment (Linux, ext4), the six F4 rows on
   the current head, and the size script below committed as a tool so every
   later step reports before/after.
2. **Correctness**: F1a, F1b, F2, F3. Each is small and independent.
3. **Room**: S1, then S2, then S3, with the measurement after each. Check
   the 8088 cost with MartyPC for S2, since the worker's per-pass guard is
   the only addition on a hot path.
4. **F1 widening** (dwords, `br_docmap` walk, Tab continuing from the last
   link), now that there is room.
5. **S4, S5, and the claims table**, written into SPEC.md 71.
6. **1.7 images** per BROWSER-IMAGES-PLAN.md, with the decoder as its own
   lazy part from day one.

## 7. What the failed 1.7 attempt teaches

Kept on `backup/browser-1.7-codex-attempt` (local branch, worktree
`C:\os8088\browser-1.5`).

- **Start from current `main`.** That work began before the review's nine
  fix commits. A 5,500-line move of `browser.asm` on a stale base cannot be
  merged and silently drops the fixes.
- **Check the limit before optimising for it.** The 726-byte headroom was
  measured against a ceiling that had already been raised.
- **One concern per change.** Parser extraction and the 768 KB EMS stress
  test ran in the same tree, so no field result could say which change
  caused what.
- **Reproduce before asking for a field run.** Ten images (r1-r10) went to
  the user's machine on hypotheses and all ten froze it. EMS runs in
  emulation here:
  - MartyPC `os8088_5150_herc_hdd_sb_ems_gla` (a Lo-tech 2 MB board) has no
    NIC, but a local file exercises the same parse and growth path;
  - 86Box's `emscard` profile with `xt-wire`'s NE1000 has both.
- **Respect the ownership rules first.** The worker grew claims and entered
  the EMS driver, and a whole-arena copy ran in one UI callback. Each patch
  added handshake state (`fin` = 6/8/9/10, `br_parsing`, lock toggles) on top
  of a design that broke SPEC.md 20.6. Decide who owns each step before
  writing it.
- **A build is not a test.** Most "passed" lines were builds and
  `t_stkapps`, run where the emulator rows could not start. Say which rows
  ran and which could not.

## Appendix: the size script

`scratchpad/brmap.py` from this audit reads `build/browser.map` and prints
bytes per top-level label, per source file, and the BSS fields by size. Turn
it into `tools/brsize.py` in step 1. Its output for the largest items:

```
code                              bss
1034 br_dl_step                   1448 bookmark records (br_bmdata, URLs)
 979 br_layout                    1280 br_hist (Back-stack URLs)
 941 br_nstep                      880 br_flds (form fields)
 761 br_parse                      800 br_lnkoff / 800 br_lnktoff
 752 br_input                      512 br_factions / 512 br_rxb
 574 br_dl_onwake                  400 br_lnkfrm
 563 br_build                      384 br_hids / 352 br_fids
 549 br_submit                     240 br_dl_reqb
 548 br_docems_ensure              ~1,600 in 160-byte URL buffers
```
