# PAGEDESK-ANY-PLAN - any PDF, on the machine

The story list for PageDesk's any-PDF round. **PAGEDESK-SPEC 20.22 is the
contract**; this file is the goal, the owner's decisions with what they were
chosen against, the stories, who builds what, the budgets and the log. A story
is ticked only when it was SEEN working (a host check against an independent
reader, a look on the glass, a number), never because it was built.

## 1. The goal

In the owner's words (2026-11-09): *"Make PageDesk able to read any PDF inside
the operating system without the python tool."*

The "python tool" is `tools/os88pdprep.py` (PD-22, PAGEDESK-SPEC 20.14). On a
modern host it rewrites a PDF into PageDesk's XT profile (PAGEDESK-SPEC 1.3)
and rasterises the pages that profile cannot draw. After this round the
machine itself opens and draws such a file, and the tool is an option, not a
requirement.

What the machine refused at the start (main `15be63d13`), from the four
scouts' reports (`~/Repos/pdany-logs/scout-*.md`):

| area | refused or substituted |
|---|---|
| structure | xref streams (`PDE_XREFSTM`); object streams; hybrid files (compressed objects read as null); damaged tables (no repair) |
| filters | LZW, ASCII85, Crypt; Flate whenever RAM held no 43 KiB slot |
| security | every `/Encrypt` (`PDE_ENCRYPT`) |
| images | DCT, CCITT, JBIG2, JPX; 2/4/16-bit samples; every colour space but DeviceGray/DeviceRGB; `/SMask`, `/Mask` |
| graphics | `sh`, shading and tiling patterns, Indexed/ICC/Cal/Lab/Separation/DeviceN; `gs` ignored (no `/ca /CA /BM /SMask`) |
| text | Type0/CID (nothing drawn), TrueType/CFF/Type1 programs (a substitute face), Type3 (a substitute), no `/ToUnicode` |

## 2. Decisions

### 2.1 The owner's (2026-11-09)

| # | question | chosen | against |
|---|---|---|---|
| O1 | how far "any PDF" reaches | **everything**, JPX at a reduced resolution level | all but JPX/JBIG2; structure and text first |
| O2 | where the new code lives | **one shared lazy slot, plus an XMS/EMS cache of module images** | one slot only; eager on bigger machines |
| O3 | encryption | **prompt for a password; a save is an incremental update encrypted with the file's key** | save a decrypted copy; read-only |
| O4 | transparency | **true blend through an 8-bit grey band** | threshold at 50% |
| O5 | the `/P` permission bits | **ignored** | honour them, with the owner password lifting them; honour them with no override |
| O6 | AES-256 (R6) on an 8088, estimated at 5-15 minutes of hashing | **ask first, with the estimate** | run it with progress |
| O7 | the 360KB disk, which has 48 KB free for about 60-110 KB of modules | **a second 360KB disk** | leave JPX/JBIG2 off the 360KB disk; move the samples off |

### 2.2 The lead's

PAGEDESK-SPEC 20.22 lists L1-L7: code goes where it runs; the module
ladder; the slot; memory; honesty; budgets; checks. The two that shape
everything else:

* **When code runs decides where it lives.** Per-byte work (decryption, LZW,
  ASCII85, xref-stream lookup, object-stream reads, compositing, shading spans)
  is resident. Interpretation-time work (colour spaces, functions, shading
  setup, ExtGState, font programs) is a slot module whose result is CACHED in
  the core. Only image decoding runs while bands are drawn. A page therefore
  swaps the slot at most once per module, and a later page of the same document
  usually swaps nothing.
* **The ladder.** A module that is needed and absent is recorded, the pass
  runs to its end, and the primary loads the module and re-runs the pass. The
  interpreter already re-runs passes (D4), so the ladder adds no new mechanism
  to the renderer, only a reason to re-run.

## 3. Stories

Status: `-` not started, `B` built on a branch, `I` integrated, `S` SEEN
(evidence in section 6), `P` partial. All are XT required unless marked.

| ID | story | owner | status | notes |
|---|---|---|---|---|
| AP-01 | open xref-stream, object-stream and hybrid files (PDF 1.5+) | eng | - | |
| AP-02 | open a damaged file by rebuilding its xref from a scan | eng | - | |
| AP-03 | LZW and ASCII85 streams, and Flate where RAM was short | eng | - | |
| AP-04 | encrypted files: the empty password silently, a prompt otherwise; RC4-40/128, AES-128, AES-256 | crypt, ui | - | |
| AP-05 | R6 on an 8088 asks first with an estimate (O6) | ui, crypt | - | |
| AP-06 | Save, Extract and Insert on xref-stream, object-stream and encrypted files; a save to an encrypted file is encrypted | wrt | - | |
| AP-07 | JPEG (baseline, progressive, CMYK/YCCK) at a reduced scale | dct | - | |
| AP-08 | CCITT G3/G4 | bilevel | - | |
| AP-09 | JBIG2, with `/JBIG2Globals` | bilevel | - | |
| AP-10 | JPEG 2000 at a reduced resolution level | jpx | - | |
| AP-11 | Indexed, ICC, Cal, Lab, Separation, DeviceN colour, PDF functions 0/2/3/4; images in those spaces and at 2/4/16 bits | gfx, rcore | - | |
| AP-12 | shading types 1-7, `sh`, shading and tiling patterns | gfx | - | |
| AP-13 | `/ca /CA`, blend modes, soft masks and transparency groups, through an 8-bit band | blend | - | |
| AP-14 | embedded TrueType, Type1 and CFF fonts from their outlines; Type0/CID with Identity and embedded CMaps; `/ToUnicode` for Find and Copy | fonts | - | |
| AP-15 | Type3 fonts | fonts | - | |
| AP-16 | the shared slot and its XMS/EMS cache | plat | - | |
| AP-17 | every geometry carries every module; the second 360KB disk (O7) | plat | - | |
| AP-18 | a real-world corpus opens with no Incomplete flag where MuPDF draws the page | lead | - | final pass |
| AP-19 | `os88pdprep.py` documented as optional; the profile (1.3), the guide and README rewritten | lead, ui | - | final pass |
| AP-20 | XT proof on MartyPC: time to the first page and memory for a page of each new kind | lead | - | final pass |

## 4. Agents, files and budgets

Each agent works in `~/Repos/os8088-pdany-<key>` on branch
`pagedesk/anypdf-<key>`, reads `~/Repos/pdany-logs/COMMON.md` first, and
edits only what it owns. "Routines" means an agent owns those functions in a
file that another agent also edits.

| key | model | owns | budget (image + bss) |
|---|---|---|---|
| plat | Opus | the slot loader, XMS/EMS cache and ladder in the primary (`pdumod.c`, `pdujob.c`, `pdui.inc`, `pdu.h`, new `pduslot.c`), the parts table (`pagedesk.asm`), the claim layout, the Makefile part rules for the five occupants with stand-in images, `tools/os88pdstk.py`, `tests/unit/t_pdstk.py`, `t_pdkgate.py`, the disk recipes (O7) | primary: frees >= 800 first (20.21.9 names `pdu_ctl_paint` as the candidate), then +900 at most |
| eng | Opus | PDENG structure: `pdexref.c`, `pdefile.c`, `pdelex.c` (but not `ob_value`'s string case), `pdestrm.c` (but not `raw_get`, `st_decode`'s nflt==0 branch, `v_strm_read`'s nflt==0 branch), `pdeverb.c` (but not the crypt verbs' bodies), `pdeng.c`, `pde.h`, `pdeng.asm`, `hosttest/pde_*` | PDENG +6,800 (cap 57,344) |
| crypt | Sonnet | the crypto library (new `pdc*.c`/`pdc*.inc`), PDXK (new `pdxk.asm`/`pdxk*.c`), PDENG's decryption in new `pdecrypt.c` plus the hooks named under eng, the CRYPTINFO/SETKEY/CRYPT bodies, `/Encrypt` parsing from the ref eng records | PDENG +3,200; PDXK <= 32,768 |
| wrt | Sonnet | `pdw*.c`, `pdwrt.c`, `pdwrt.asm`, `pdw.h`, `hosttest/pdw_*` | PDWRT +5,000 (cap raised to 53,248) |
| ui | Sonnet | new `pduauth.c` (primary) and `pdovlauth.c` (overlay), `pduerr.c`, `pdupref.c`, `pdovlpref.c`, `pdovl.c`'s help text, `pducmd.c`'s popover arms | primary +250; overlay +4,000 (cap raised to 53,248) |
| rcore | Opus | `pdri.c` (but not fonts' routines or the operator arms others own), `pdrv.c`, `pdrend.c`, `pdrend.h`, `pdrend.asm`, `pdrxc.c`, `pdr_imgrow` in `pdrprim.inc`, `hosttest/pdr_*`; moves the dash walker out to new `pdxdash.c` for PDXG | PDREND: frees >= 2,400 first, then +1,400 |
| fonts | Sonnet | `pdrf.c`; in `pdri.c` `font_res`, `text_show`, `text_glyph`, `glyph_emit` and the text operators' arms; in `pdrd.c` `strike_pick`, `strike_load`, `draw_glyphs`; Type3; PDXF (new `pdxf.asm`/`pdxf*.c`) | PDREND +1,100; PDXF <= 61,440 |
| gfx | Sonnet | PDXG's shell (new `pdxg.asm`/`pdxg.c`) and its colour, function, shading and pattern files; core glue in new `pdrxg.c`; in `pdri.c` the `cs CS sc SC scn SCN sh d` arms; in `pdrd.c` the walker arms for its own record types | PDREND +700; PDXG <= 61,440 shared with blend |
| blend | Sonnet | `pdrd.c`'s raster (`walk_band`, `span_out`, `band_*`, the sinks), new `pdrx8.c`/`pdrx8.inc`, core glue in new `pdrxb.c`, the `gs` arm and `do_form`'s group begin/end calls, ExtGState in new `pdxgtr.c` (included by PDXG) | PDREND +2,400 |
| dct | Sonnet | PDXI's shell (new `pdxi.asm`/`pdxi.c`, the IMG verbs), new `pdxidct.c` (+ asm) | PDXI <= 65,024 shared with bilevel: DCT <= 24,576 |
| bilevel | Sonnet | new `pdxiccit.c`, `pdxijb2.c` (+ asm), included by PDXI with one line each | inside PDXI: <= 36,864 |
| jpx | Sonnet | PDXJ (new `pdxj.asm`/`pdxj*.c`) | PDXJ <= 65,024 |

Every agent also owns its `tools/os88pdfx_<key>.py`, its `tests/pdfany/<key>/`,
its host harness files and its own PAGEDESK-SPEC subsection.

**Sizes at the start** (after the lead's ABI commit; image + bss, bytes):
primary 64,147 of 65,024; overlay 48,490; PDENG 47,152; PDREND 61,308; PDWRT
47,796.

## 5. Waves

* **Wave 1, everything in parallel.** Each agent writes its PAGEDESK-SPEC
  subsection first, then builds and proves its piece on the host against the
  pinned ABI. Where a piece it needs is not yet built, it works against the
  lead's stand-ins (`pdrxc.c`; the primary's slot, once plat lands it).
* **Integration.** No review between steps (the owner's word, 2026-11-09).
  As each agent reports DONE, the lead merges its branch in this order: docs/tools, then PDENG (eng, crypt), then the
  primary and slot (plat, ui), then PDREND (rcore, fonts, gfx, blend), then the
  occupants (dct, bilevel, jpx), then the writer. After each merge: `make`, the
  size checks, `kernsize`.
* **QA in emulation, once every feature is in** (the owner's word,
  2026-11-09): one testing run on the merged head. QEMU on VGA, CGA and
  Hercules, and MartyPC for the XT, open a corpus with every new kind of file.
  Each page is compared with MuPDF, and saves, the password prompt, the R6
  question and memory are checked. Every bug found goes into a list in
  section 6.
* **Fix round.** The QA list is fixed, the failing cases are re-run, and the
  deduplicated soak union and `test-full` run once. Then AP-20 on MartyPC, and
  screenshots of the merged head.

## 6. Evidence

Links per story as they are taken. Until the final pass the evidence is the
agents' reports in `~/Repos/pdany-logs/` (`*-report.md`, `*-verify.md`).

## 7. Log

- 2026-11-09: goal; four scouts (platform, engine, render, writer); owner
  decisions O1-O7; ABI and contract (20.22) pinned; wave 1 launched.
