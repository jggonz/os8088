# PAGEDESK-PLAN - building PageDesk

The living plan for PageDesk (SPEC.md 127). **docs/PAGEDESK-SPEC.md is the
contract**; this file is the story list, the platform record it was written
against, who builds what, and the capability matrix. A story is ticked only
when it was SEEN working (a host check, a look on the glass, a number), never
because it was built.

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
| PD-01 | launch as a native application | XT | 1 | shell | - | |
| PD-02 | full VGA workspace | XT | 1-2 | shell | - | |
| PD-03 | CGA 200-line workspace | XT | 1-2 | shell | - | |
| PD-04 | Hercules monochrome | XT | 1-2 | shell | - | |
| PD-05 | self-contained fonts and graphics | XT | 1 | fonts | - | |
| PD-06 | open real PDFs, bounded | XT | 1 | eng | - | |
| PD-07 | render text, vectors, images | XT | 1 | rend | - | |
| PD-08 | responsive and bounded rendering | XT | 1-2 | rend, shell | - | |
| PD-09 | navigate and scale | XT | 2 | | - | |
| PD-10 | select, copy, find | XT | 2 | | - | |
| PD-11 | thumbnails, outlines, links | XT | 2 | | - | |
| PD-12 | notes | XT | 2 | | - | |
| PD-13 | highlight / area mark | XT | 2 | | - | |
| PD-14 | text boxes | XT | 2 | | - | |
| PD-15 | ink, shapes, signature | XT | 2 | | - | |
| PD-16 | changes, undo/redo | XT | 2 | | - | |
| PD-17 | save a real PDF | XT | 1-2 | wrt | - | wave 1: serialiser, appearances, fresh documents; wave 2: incremental save on PDENG |
| PD-18 | reorder/rotate/delete/insert pages | XT | 2 | | - | |
| PD-19 | extract / insert from another PDF | XT | 2-3 | | - | |
| PD-20 | new document, recovery | XT | 2 | | - | |
| PD-21 | reproducible samples | XT | 1 | fix | - | |
| PD-22 | host preparation of complex PDFs | H | 3 | | - | |
| PD-23 | keyboard and file workflows | XT | 1-2 | shell | - | |
| PD-24 | package and disk images | XT | 1 | shell | - | |
| PD-25 | more RAM, Flate | E | 3 | | - | |
| PD-26 | AcroForm filling | E | 3 | | - | |
| PD-27 | protected-mode decision/spike | X | 3 | | - | |
| PD-28 | malformed input, failing storage | XT | 1-3 | eng, fix | - | |
| PD-29 | hardware, memory, responsiveness proof | XT | final | lead | - | |
| PD-30 | documented working behaviour | XT | final | lead | - | |

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

Filled in as stories are SEEN; `planned` until then.

| capability | XT | 286/386 | host-prepared |
|---|---|---|---|
| classic xref, incremental chains | planned | planned | - |
| ASCIIHex, RunLength | planned | planned | - |
| Flate | refused (`PDE_FLATE`) | planned (PD-25) | normalised by PD-22 |
| base-14 Latin text | planned | planned | - |
| other fonts | substituted, Incomplete | substituted | rasterised by PD-22 |
| 1/8-bit grey images | planned | planned | - |
| JPEG, CCITT, JBIG2, JPX | refused | refused | rasterised by PD-22 |
| annotations: Text, Highlight, Square, Circle, Line, Ink, FreeText | planned | planned | - |
| AcroForm | read-only appearance | planned (PD-26) | - |
| encryption | refused | refused | - |

## 7. Evidence

Links to screenshots, host-check output and measurements, per story, as
they are taken.

## 8. Log

- 2026-11-05: contract and plan written; wave 1 launched.
