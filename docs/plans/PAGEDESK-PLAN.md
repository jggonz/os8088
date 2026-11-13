# PAGEDESK-PLAN - building PageDesk

The living plan for PageDesk (SPEC.md 128). **docs/PAGEDESK-SPEC.md is the
contract**; this file is the story list, the platform record it was written
against, who builds what, and the capability matrix. A story is ticked only
when it was SEEN working (a host check, a look on the glass, a number), never
because it was built.

The reader's guide is `docs/PAGEDESK-GUIDE.md`; how it was built, wave by
wave, with the decisions and the discoveries, is
`docs/HOW-PAGEDESK-WAS-BUILT.md`.

## 1. The goal

In the owner's words: *"Build out PageDesk. There may be existing PDF
exporting functionality in Tex, but we may want to extend that or create a
better PDF engine using opensource code from online sources. The attached
images show the design."* The full assignment (30 stories, PD-01 to PD-30)
is the owner's `PAGEDESK_PROMPT.md`, with three mockups: a Hercules
monochrome workspace with thumbnails and a notes panel, the VGA colour
workspace with the Text properties inspector, and the CGA compact workspace
with a note popover.

The baseline is an IBM PC/XT, 8088 at 4.77 MHz, 640 KiB, no FPU/EMS/XMS. A
protected-mode version is an optional later extension, never a substitute.

## 2. The platform record (inspected 2026-11-05, main `86fc03da1`)

| fact | value | where |
|---|---|---|
| package image + bss | `APP_MAX_SIZE` = 0xFE00 = 65,024 bytes, enforced by `tools/os88pkg.py` and `ld_check_hdr` | SPEC.md 20.1.1, kernel/loader.inc |
| a part (`OP_SEG`/`OP_ASSET`) | each at most `OP_PARTMAX` = 0xFE00; eager carve under 1,920 sectors | tools/os88pkg.py, SPEC.md 20.12 |
| C overlay | one `ovl_*` module per package, `CC_OVL_PART`, UI task only | SPEC.md 73.14 |
| further C segments | a separately compiled module with its own DS, door at +8 | apps/loom/lmpvmod.asm (WEAVE-SPEC 1.2.4) |
| C rules | no `&local`, no string instructions, no `long`/`float`, frames <= 96 bytes; `tools/cc8086.py` gates each | SPEC.md 73, docs/C-TOOLCHAIN.md |
| stacks | UI task 512 bytes, worker <= 384 | SPEC.md 73.8 |
| heap on a 640 KiB XT, kern_big | about 534 KiB free after boot | docs/KERNEL-MEMORY.md |
| claims | `OSAPI_MEM_P` paragraphs (512-byte granule), `OSAPI_MEM_AVAIL`, `OSAPI_MEM_HUGE`; parts may not claim | SPEC.md 50.3.7, 20.12.5 |
| extended memory | `OSAPI_XMEM_*` (data store, 32 KiB a copy), `EMS.DRV` through `OSAPI_DRV_CALL` | SPEC.md 41.11, 107 |
| reads | `OSAPI_FILE_READ_SEQ` (kern_big): streaming, 32-bit cursor offset, cluster-multiple seek and capacity, delivers a file RAW (a `CZ`-packed file as packed bytes) | SPEC.md 18.4.8 |
| writes | `OSAPI_FILE_WRITE_SEQ`/`APPEND` streaming append; `WRITE_AT` in place | SPEC.md 18.4.7, 18.4.9 |
| file size | dword; FAT16 volumes to ~2 GB | SPEC.md 52.3.1 |
| replace | none atomic: `RENAME` refuses an existing name; temp + delete + rename is the house convention (PiXEL, WaveRack) | SPEC.md 18.4, 106.24, 123.10 |
| worker rule | no `OSAPI_FILE_*`, no file dialog, no overlay call from a worker | SPEC.md 20.6 rule 7 |
| file dialog | `OSAPI_FILE_DLG`, no type filter | SPEC.md 38.6 |
| association | header `OS88_ASSOC*` + `OSAPI_ARG_FILE`, document glyph `OS88_DOCGLYPH8` | SPEC.md 54 |
| clipboard | text, 32 KiB ceiling | SPEC.md 55 |
| geometry | `OSAPI_VIDEO`/`OSAPI_WM_DISPLAY`: VGA 640x480, EGA 640x350, Hercules 720x348, CGA 640x200; no aspect slot | SPEC.md 39.2 |
| printing | none (no driver class, no slot) | SPEC.md 68.11 |
| PDF in the tree | TeXPad writes PDF 1.4 (Courier, 16-bit offsets, 24 KiB cap); nothing reads PDF | SPEC.md 69.5, apps/texpad/tpexport.inc |
| type | `.F88` single-size faces <= 8 px wide (HELV, TIMES, COURIER, OFL); no base-14 AFM widths in the tree | SPEC.md 6.4, faces/ |
| host PDF tools | qpdf 12, MuPDF `mutool`, Poppler `pdftoppm` (Homebrew); no Python PDF library | this machine |
| build | `make` (fast tier included), `make KERN_SMALL=1`, `tools/kernsize.py`, `make budget-check` | CLAUDE.md |
| tests | `tools/os88test.py` over `tests/suite.py`; emulator rows once, in the final pass | CLAUDE.md *Testing* |

What the assignment's numbers became: the 176 KiB planning ceiling is the
**idle** budget and 224 KiB the **peak** (PAGEDESK-SPEC 16.1), justified by
C's 2-4x code size against assembly and still leaving over 300 KiB of the
534 KiB heap unclaimed at peak. The old 60/63.5 KiB limit is 65,024 bytes per
segment today.

### 2.1 Decisions taken by the lead

| # | decision | against | why |
|---|---|---|---|
| L1 | C for components, assembly for inner loops and arithmetic | all assembly | every parser/writer component is then host-compiled and tested against qpdf/MuPDF/Poppler; SPEC.md 73.11 keeps pixels in assembly |
| L2 | three far-called C modules + primary + overlay + asset part, one `.O88` | one segment | the code cannot fit 65,024 bytes; LOOM.WPV is the precedent |
| L3 | no worker task: jobs stepped from `OSAPI_WM_ONWAKE` | a PiXEL-style worker | the parser needs file I/O and overlays mid-operation, both forbidden on a worker |
| L4 | xref by subsection table + 20-byte entry reads | an index in RAM | unbounded object counts in bounded memory, by the format's own fixed width |
| L5 | Print omitted | an inert button | no print service exists (SPEC.md 68.11); the assignment says omit |
| L6 | Flate is Enhanced (PD-25), reusing PiXEL's inflater (`apps/pixel/pxpng.asm`) when promoted | XT required now | its 32 KiB history must be measured against the XT gates first |
| L7 | the sample kit the assignment mentions was not supplied; `tools/os88pdfgen.py` generates every sample deterministically | waiting | nothing blocks on it; a supplied kit is audited and added as fixtures when it arrives |
| L8 | the shipping disk stores the samples RAW (not `PKGZ`-packed) | packed | `READ_SEQ` delivers a file as it lies; WaveRack's clips are the precedent |

## 3. Stories

Status: `-` not started, `B` built (on a branch), `I` integrated, `S` SEEN
(evidence linked in section 7), `P` partial (what is missing is written in
the notes). Label: XT = XT required, H = Host assistance, E = Enhanced, X =
Exploratory.

| ID | story | label | wave | owner | status | notes |
|---|---|---|---|---|---|---|
| PD-01 | launch as a native application | XT | 1 | shell | S | launch, dock icon, .PDF association, empty view: shell-report, shell-verify |
| PD-02 | full VGA workspace | XT | 1-2 | shell | S | FULL layout on VGA 640x480: shell-verify, lead looks |
| PD-03 | CGA 200-line workspace | XT | 1-2 | shell | S | COMPACT on CGA 640x200: shell-verify, nav-verify (real PDREND) |
| PD-04 | Hercules monochrome | XT | 1-2 | shell | S | Hercules 720x348 (HERCSEG 0x7000): shell-report, rendfix-report |
| PD-05 | self-contained fonts and graphics | XT | 1 | fonts | S | PDRES fonts and art: fonts-report |
| PD-06 | open real PDFs, bounded | XT | 1 | eng | S | classic xref, incremental chains, bounded tables: eng-report, eng-verify |
| PD-07 | render text, vectors, images | XT | 1 | rend | S | text, vectors, images, annotation appearances: rend, rendfix, annotr |
| PD-08 | responsive and bounded rendering | XT | 1-2 | rend, shell | P | banded, cancellable, sliced (seen). XT timings in 7.1: a page is 20-58 guest s, not 10 s of CPU; the per-slice and cancel latencies are not measured |
| PD-09 | navigate and scale | XT | 2 | | S | zoom/fit/go-to/history/scroll drag: nav-verify |
| PD-10 | select, copy, find | XT | 2 | | S | selection, Copy, Find next/previous: nav-verify (VGA + CGA) |
| PD-11 | thumbnails, outlines, links | XT | 2 | | S | thumbnails, outline, links: nav-verify (VGA) |
| PD-12 | notes | XT | 2 | | S | note popover, panel, resolve/reopen: notes-report, lead look |
| PD-13 | highlight / area mark | XT | 2 | | S | Highlight from a selection, area mark: edit-verify, nav-verify |
| PD-14 | text boxes | XT | 2 | | S | text boxes, Text properties: edit-verify, notes-report |
| PD-15 | ink, shapes, signature | XT | 2 | | S | ink, line/arrow, rect, ellipse, drawn signature (one stroke): edit-verify |
| PD-16 | changes, undo/redo | XT | 2 | | S | undo/redo incl. page operations: edit-verify, lead look |
| PD-17 | save a real PDF | XT | 1-2 | wrt | S | fresh + incremental save, D14 cross-folder, verified through PDENG and qpdf: wrt, pages |
| PD-18 | reorder/rotate/delete/insert pages | XT | 2 | | S | reorder/rotate/delete/duplicate/blank, undo at the wake: pages-verify, lead look |
| PD-19 | extract / insert from another PDF | XT | 2-3 | | S | Extract (pages-verify) and Insert from PDF (20.16): colliding object numbers and font names, qpdf --check, pixels equal to the source pages, seen on VGA and CGA (insert-report, insert-verify) |
| PD-20 | new document, recovery | XT | 2 | | S | New; the recovery journal, the finish-save offer and the window close ask (20.17), seen on VGA and CGA (recov-verify). New/Open/Close document do not ask (20.20.2) |
| PD-21 | reproducible samples | XT | 1 | fix | I | tools/os88pdfgen.py, samples/, MANIFEST: fix-report |
| PD-22 | host preparation of complex PDFs | H | 3 | | I | tools/os88pdprep.py, row pdprep: host-report |
| PD-23 | keyboard and file workflows | XT | 1-2 | shell | S | keys, menus and Preferences (20.18): settings on the launch disk, read-only media handled, seen on VGA and CGA (prefs-verify) |
| PD-24 | package and disk images | XT | 1 | shell | I | PAGEDESK.O88 + parts, `make pagedeskdisk` in four geometries |
| PD-25 | more RAM, Flate | E | 3 | | P | Flate + predictors decoded when RAM holds a slot (D17), seen on VGA; Preferences turns it Off; XMEM caches not built |
| PD-26 | AcroForm filling | E | 3 | | P | forms shown as printed and the not-supported note (20.18), seen on VGA; FILLING not built (D24, Enhanced, not selected) |
| PD-27 | protected-mode decision/spike | X | 3 | | I | docs/plans/PAGEDESK-PM-ADR.md: host-report |
| PD-28 | malformed input, failing storage | XT | 1-3 | eng, fix | P | malformed input: eng fuzz, tests/pdfneg, pdefuzz (green in the final pass); a failed read mid-token: pde_check's 240 fault cases and pdr_cache (verify-perf); failing storage: wrt/pages refusals, not driven on a real failing disk |
| PD-29 | hardware, memory, responsiveness proof | XT | final | lead, rows, perf | P | `pagedeskxt` on MartyPC (VGA, CGA, Hercules at 640 KiB) and `pagedesk` on QEMU, green at fef1fcbd0 (7.1). Missed: 176 KiB ceiling (375-415 KiB measured), 10 s first page (38-41 s), 32 KiB raw during Save As on CGA and Hercules (16-18 KiB; 65 with the caches shed). Not run: repeat-loop stability, a second app alongside, physical hardware |
| PD-30 | documented working behaviour | XT | final | lead | I | README.TXT, docs/PAGEDESK-GUIDE.md, docs/HOW-PAGEDESK-WAS-BUILT.md, PAGEDESK-SPEC 20.20 (docs-report) |

## 4. Waves

**Wave 1 - components against the pinned contract, in parallel.** Each agent
builds one segment or tool and proves it on the host; the lead integrates them
into the first vertical slice (launch, open `HELLO.PDF`, render it, add a
note, save, reopen, independent check).

**Wave 2 - the stories on the slice.** Navigation/search/thumbnails, the
annotation tools and inspector, undo, incremental save on PDENG, page
operations, New and recovery, keyboard flows, robustness.

**Wave 3 - host assistance and enhanced.** PD-22, PD-25 (Flate, XMEM
caches), PD-26, PD-27's decision record.

**Final pass.** The deduplicated soak union and `test-full` once; PD-29's
matrix on MartyPC (cycles) and QEMU (function), all three adapters at
640 KiB; screenshots; PR body.

## 5. Wave 1 agents

Every agent works in its own worktree `~/Repos/os8088-pd-<key>` on branch
`pagedesk/<key>`, reads `~/Repos/pagedesk-logs/COMMON.md` first, and owns
exactly the files listed. Reserved PAGEDESK-SPEC subsections: an agent adds
detail only under its own 20.N.

| key | model | builds | owns | spec |
|---|---|---|---|---|
| math | Sonnet | `pdmath.inc` (8086 bodies of `pdmath.h`), `pdabi.inc`, `tools/os88pdabi.py --check`, the target/host differential test | `apps/pagedesk/pdmath.inc`, `apps/pagedesk/pdabi.inc`, `tools/os88pdabi.py`, `tests/pdmath/` | 20.7 |
| fix | Sonnet | `tools/os88pdfgen.py`, `tools/os88pdfcheck.py`, the 8 samples + manifest, stress set, negative corpus | `tools/os88pdfgen.py`, `tools/os88pdfcheck.py`, `apps/pagedesk/samples/`, `tests/pdfneg/` | 20.1 |
| eng | Sonnet | `PDENG.MOD`: file layer, lexer, objects, xref, pages, filters, text strings, resources, outline, annots; host harness; C SDK thunks it and the writer need | `apps/pagedesk/pdeng.asm`, `pde*.c`, `hosttest/pde*`, `apps/cc/os88.h` + `os88thunk.asm` (new thunks only) | 20.2 |
| rend | Sonnet | `PDREND.MOD`: interpreter, paths, band rasteriser, glyphs, images, text map, thumbnails; host harness rendering to PBM/PGM | `apps/pagedesk/pdrend.asm`, `pdr*.c`, `pdr*.inc`, `hosttest/pdr*` | 20.3 |
| fonts | Sonnet | `tools/os88pdfont.py`, the `PDFB` bank, base-14 width tables, icons/cursors/app icon/doc glyph, `PDRES.BIN` | `tools/os88pdfont.py`, `tools/os88pdart.py`, `apps/pagedesk/fonts/`, `apps/pagedesk/art/` | 20.4 |
| wrt | Sonnet | `PDWRT.MOD`: serialiser, appearance streams for every PAGEDESK-SPEC 13.3 kind, fresh-document writer, xref/trailer emission; host harness checked by qpdf/MuPDF | `apps/pagedesk/pdwrt.asm`, `pdw*.c`, `hosttest/pdw*` | 20.5 |
| shell | **Opus** | the package: shim and parts table, module loading and INIT, claims, the job scheduler, the three layouts with every bar and panel, menus and keys, the file dialog and association, stub modules until the real ones land, the build rules and `make pagedeskdisk` | `apps/pagedesk/pagedesk.asm`, `pagedesk.c`, `pdu*.c`, `pdovl.c`, `apps/pagedesk/stubs/`, the Makefile's package and disk rules | 20.6 |

`shell` runs on Opus because it is the spine everyone integrates into:
module loading, claim ownership and teardown are where "the wrong thing
freed" lives.

## 6. Capability matrix

XT means seen on the 8088 path, on MartyPC or by the target-vs-host A/B. On
286/386 the same 8086 code runs, but no 286/386 machine has been run (PD-25's
XMEM caches are not built).

| capability | XT | 286/386 | host-prepared |
|---|---|---|---|
| classic xref, incremental chains | yes (PD-06; pde_boot target == host on 145 files) | same code, not run | - |
| ASCIIHex, RunLength | yes (eng fixtures) | same code, not run | - |
| Flate | decoded when RAM holds a 43 KiB slot (D17, by RAM never by CPU), else refused `PDE_FLATE` | same | normalised by PD-22 |
| base-14 Latin text | yes (HELLO, XTGUIDE on MartyPC) | same code, not run | - |
| other fonts | substituted, Incomplete | substituted | rasterised by PD-22 |
| 1/8-bit grey images | yes (rend fixtures, pdrend-real) | same code, not run | - |
| JPEG, CCITT, JBIG2, JPX | refused | refused | rasterised by PD-22 |
| annotations: Text, Highlight, Square, Circle, Line, Ink, FreeText | yes (annotr; a saved /Square re-opens with all four edges, `pagedesk`) | same code, not run | - |
| AcroForm | read-only appearance | read-only appearance; filling not built (D24) | - |
| encryption | refused | refused | - |

## 7. Evidence

Links to screenshots, host-check output and measurements, per story, as
they are taken. Until the final pass, the evidence is in the agents' reports
and screenshots in the lead's log directory (`~/Repos/pagedesk-logs/`,
`*-report.md`, `*-verify.md`, `shots/<key>/`); the PR body carries the
selection that ships with the merge.

### 7.1 PD-29: the final pass (fef1fcbd0, 2026-11-09)

The soak union ran 17 rows on the merged tree, all green. It needed one
declaration fix to the `pagedesk` row. The renderer and end-to-end rows were
re-run after the mono stroke fix (20.19), and `make test-full` was 101/0
alone. Each step is in guest seconds on a 4.77 MHz 8088 with 640 KiB, on
MartyPC. These are emulated and cycle-modelled, not physically run; floppy
time is the emulator's.

| step | VGA (5160) | CGA (5150) | Hercules (5150) | VGA before perf |
|---|---|---|---|---|
| HELLO: double-click to first page | 40.8 | 38.3 | 38.4 | 139.7 |
| XTGUIDE: chooser to first page | 39.5 | 30.6 | 31.9 | 111.9 (a) |
| page turn (PgDn) | 35.5 | 23.5 | 31.1 | 155.0 |
| zoom in | 21.5 | 16.0 | 17.4 | 138.5 |
| Find 'Index' across XTGUIDE's 12 pages | 87.4 | 87.0 | 86.8 | 1,427.8 |
| area mark drawn | 13.2 | 11.5 | 12.5 | 99.3 |
| Save As XTMARK.PDF | 31.5 | 31.6 | 31.8 | 36.3 (a) |
| re-open after Save As | 58.3 | 50.3 | 53.7 | 143.0 |
| free after the session (KiB) | 97.0 | 83.0 | 81.0 | 83.0 (a) |
| lowest unclaimed during Save As (KiB, raw / caches shed) | 32.0 / 103.0 | 18.0 / 65.0 | 16.0 / 65.0 | 31.0 / - (5.5 / 76.5 before D28 (a)) |
| PageDesk's own claims + its instance (KiB) | 317.5 + 58 | 357.5 + 58 | 357.5 + 58 | - |
| task 0 stack (D22 bound 384) | 320 | 320 | 320 | 320 (a) |
| private module stack (D23 bound 1792) | 322 | 322 | 342 | 354-372 (a) |

The "before" column is perf's clean build of dcd0b85e0. Cells marked (a) come
from the rows agent's run at 8e00bef0d, before D28. CGA and Hercules carve
two Flate slots (the 146 KiB document block) where VGA carves one (106 KiB);
that is D17 choosing by RAM.

Every saved XTMARK.PDF passes `qpdf --check` (exit 0) with its /Square and
XTGUIDE's 7 /Link. On QEMU, `pagedesk` drives Insert from PDF, Notes,
Preferences and Save As on VGA and CGA. The re-opened mark draws 100% of all
four edges.

What closed the gap: perf found the lexer re-reading a font's `/Widths` from
element 0 for every glyph, which was 92% of all tokens. It added dict_get's
array cursor, the lexer's inner loops in assembly, a one-walk widths fill and
per-document font caches. verify-perf's Tf-cache fix took XTGUIDE's
RES_LOOKUPs from 314 to 43, so Find went from 144 to 87 s. The 20 KiB glyph
cache was measured (about 4% faster, 8 KiB less heap) and declined.

### 7.2 Open items

- **Performance:**
  - PD-29's 10-second first page is missed by about 4x. Where the time goes:
    PDENG `lx_token` (needs bytes PDENG doesn't have: 6 free), floppy reads
    (18-20% of a draw), the kernel's VGA drawing (about 12%), and multi-pass
    pages re-lexing their content.
  - Per-slice (100 ms) and cancel (250 ms) latencies are not measured.
- **Memory:**
  - The SPEC 16.1 176 KiB ceiling is not met (16.1 Measured).
  - Save As dips under 32 KiB raw on CGA and Hercules (16-18 KiB); it is
    held with the purgeable caches shed (65 KiB).
- **PD-29 not run:** repeat open/edit/save/close loops, a second application
  alongside, physical hardware.
- **Enhanced:** PD-25's XMEM caches and PD-26's form filling are not built.
- **Behaviour:**
  - New, Open and Close document don't ask Save or Discard (only the window
    close does).
  - The forms/scripts refusal misses /JS placed directly in an annotation
    dict.
  - Thumbnails index by view position, not page id.
  - Name-tree link targets are not followed.
  - A normal redraw shows old pixels until the first band.
  - A rotated or skewed form BBox still clips by pixel centres.
  - A font whose load fails a read can keep a fallback width, and its page
    draws without the Incomplete flag.
- **Platform:**
  - kern_mega's option-ROM IRQ on the private module stack is unexamined.
  - CC_HAS_ONDRAG in crt0 is deferred.
  - Copy's transient claim has no 16.2 row.

## 8. Log

- 2026-11-05: contract and plan written; wave 1 launched.
- 2026-11-05: wave 1 integrated (eng, wrt, math, fonts, fix, shell, rend); wave 2 launched.
- 2026-11-05: wave 2 integrated (edit, notes, nav, pages, annotr, flate, host) with D23's private module stack (20.15) and the static stack gate (`pdstk`).
- 2026-11-06: wave 3 launched: insert (PD-19), recov (PD-20), prefs (PD-23, PD-26's baseline), polish (carried defects). D24 budgets.
- 2026-11-09: wave 3 integrated after a usage-limit pause; every segment at its cap (primary 59,235 of 59,392). PD-30's docs merged.
- 2026-11-09: final round. The rows agent added 14 host rows plus `pagedesk` (QEMU) and `pagedeskxt` (MartyPC). square fixed the re-opened /Square's lost edges (outward BBox clip). perf and verify-perf made the XT session 5.7x faster. D28 reserves room for a save beside the Flate slots. The final soak union was green; see 7.1.
- 2026-11-09: the face (PAGEDESK-SPEC 20.21), after the merge, at the owner's word that the shipped UI did not look like the design. Host-built kit (`PDUK`, PDRES kind 9: Liberation Mono 10/10/12 px and 28 hand-drawn icons in `apps/pagedesk/art/face.kit`), one band per control through `apps/pagedesk/pdkit.inc`, bevelled bars, the desk and page shadow, teal thumbnails, Notes cards and a three-field status line. Primary 64,117 of 65,024 (decision 1 in 20.21.9: past D24's 59,392); the INDEX claim now taken at launch (decision 2). The pdstk gate's worst UI chain is 364 against 366; 20.21.9 records that the gate does not charge the primary's SDK-thunk drawing calls.
