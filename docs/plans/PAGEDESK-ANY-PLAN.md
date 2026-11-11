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
| O8 | the font bank (PDRES) on the 360KB disk (2026-11-10) | **packed inside PAGEDESK.O88 as LZ4 blocks, one stream a strike** | a third disk; fewer strikes |
| O9 | PD-29's 32 KiB reserve, which the decoders and the real 16 KiB edit store broke on 640 KB (2026-11-10) | **restore it**: the occupant released when PageDesk is idle, the edit store 8 KiB (64 marks a document), the Save As trough fixed | relax the contract to measured floors; release the decoder only |

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
| AP-01 | open xref-stream, object-stream and hybrid files (PDF 1.5+) | eng | S | QA: the real-world corpus (PDF 1.5-1.7 from Quartz, Word, MuPDF, Ghostscript) opens; MUDAMG (R1) |
| AP-02 | open a damaged file by rebuilding its xref from a scan | eng | S | MUDAMG and the gen/ damaged fixtures rebuilt by the scan (R1) |
| AP-03 | LZW and ASCII85 streams, and Flate where RAM was short | eng | S | LZW held to qpdf at chunks 1/37/4096 (`make pdflate`); Flate on a 640 KB XT (F22) |
| AP-04 | encrypted files: the empty password silently, a prompt otherwise; RC4-40/128, AES-128, AES-256 | crypt, ui | S | GUIDE_AES, GUIDE_RC4, GUIDE_R6, A128, INPW/INR40/INAES; RC4 23 KB/s, AES-128 3.4 KB/s on an 8088 |
| AP-05 | R6 on an 8088 asks first with an estimate (O6) | ui, crypt | S | the estimate is 12.75 x (pw+48) s, MartyPC-calibrated (F23); Cancel works |
| AP-06 | Save, Extract and Insert on xref-stream, object-stream and encrypted files; a save to an encrypted file is encrypted | wrt | S | F11/F12/F17/F25 saves pass `qpdf --check`; Insert from an encrypted source carries /Contents (F12) |
| AP-07 | JPEG (baseline, progressive, CMYK/YCCK) at a reduced scale | dct | S | baseline, progressive, CMYK; XT times in section 6 |
| AP-08 | CCITT G3/G4 | bilevel | S | every CCITT fixture (R22) |
| AP-09 | JBIG2, with `/JBIG2Globals` | bilevel | S | with globals: text, huffman, halftone, refagg (R23, F15) |
| AP-10 | JPEG 2000 at a reduced resolution level | jpx | S | at a reduced level; XT time in section 6 |
| AP-11 | Indexed, ICC, Cal, Lab, Separation, DeviceN colour, PDF functions 0/2/3/4; images in those spaces and at 2/4/16 bits | gfx, rcore | S | COLOUR.PDF 48 of 48 cells against `mutool draw -M 0` (R10) |
| AP-12 | shading types 1-7, `sh`, shading and tiling patterns | gfx | P | shadings 1-7 and tiling seen; more than 40 coloured tiles and pattern strokes are OI-6 |
| AP-13 | `/ca /CA`, blend modes, soft masks and transparency groups, through an 8-bit band | blend | P | alpha, blend, groups and soft masks draw (R9); colour under transparency is OI-5 |
| AP-14 | embedded TrueType, Type1 and CFF fonts from their outlines; Type0/CID with Identity and embedded CMaps; `/ToUnicode` for Find and Copy | fonts | P | TrueType, Type1, CFF, CID and ToUnicode seen (R13, R17, R24); no CJK face; QUARTZ on 640 KB is OI-1 |
| AP-15 | Type3 fonts | fonts | S | FTYPE3, including after another file (R26) |
| AP-16 | the shared slot and its XMS/EMS cache | plat | S | slot ladder; XMS cache seen on QEMU (`d6 c6 x1`); the XC grant is OI-8 |
| AP-17 | every geometry carries every module; the second 360KB disk (O7) | plat | S | every geometry; the 360 KB pair (F21, `pd360`) |
| AP-18 | a real-world corpus opens with no Incomplete flag where MuPDF draws the page | lead | P | the corpus opens; the pages that still say Incomplete are the open items |
| AP-19 | `os88pdprep.py` documented as optional; the profile (1.3), the guide and README rewritten | lead, ui | S | PAGEDESK-SPEC 1.3, the guide and README |
| AP-20 | XT proof on MartyPC: time to the first page and memory for a page of each new kind | lead | S | MartyPC 5150 640 KB, CGA and Hercules: section 6 |

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

Links per story as they are taken. The working evidence is the agents' reports
in `~/Repos/pdany-logs/` (`*-report.md`, `*-fix-report.md`) and the two QA
files, `qa-qa-render.md` (rows R1-R30) and `qa-qa-flows.md` (F1-F34). Each
has a status per build: build 3 (1c1666c02), the re-run (4b0dc816b), re-check
2 (7acb28a1c). The PR carries the screenshots of the merged head.

**QA in emulation** (the owner's order: every feature first, then one QA run,
then the fixes). qa-render: QEMU on VGA, CGA, EGA and Hercules, a real-world
corpus of 60-odd files from macOS's own `/System`, `/Library` and
`/Applications` plus generated fixtures, each page against `mutool draw`.
qa-flows: opens, saves (`qpdf --check` on every save), the password prompt,
the R6 question, the 360 KB pair and memory, on QEMU and on MartyPC.

**The XT** (AP-20; MartyPC, IBM 5150 at 4.77 MHz, 640 KB, the program on a
720 KB B:; guest seconds from the double-click to the first page drawn, every
page `No changes`; build 3 in brackets; *kbd* is the longest stretch the UI
task held the machine without reading keys, which is OI-2/OI-3):

| file | CGA | kbd | Hercules | kbd |
|---|---|---|---|---|
| HELLO (Flate) | 49.1 (48.9) | 21.0 | 48.8 | 21.8 |
| XTGUIDE | 60.6 (55.2) | 19.4 | 62.9 (57.6) | 19.5 |
| JPEG | 84.8 (76.3) | 54.6 | 99.0 (84.3) | 49.9 |
| JPEG 2000 | 97.0 (91.4) | 69.3 | 98.6 | 62.9 |
| JBIG2 text | 52.0 (44.0) | 24.7 | 57.2 | 23.0 |
| CCITT G4 | 50.9 (48.5) | 25.3 | 48.0 | 20.8 |
| embedded TrueType (TTSIMPLE) | 123.2 (80.8, substituted) | 26.7 | | |
| Quartz embedded fonts (QUARTZ) | 267.6 (169.1, refused) | 44.4 | | |
| AES-128 (A128) | 78.7 (35.7, refused) | 22.4 | 69.6 | 22.9 |
| a 14-language licence, page 1 (LICEN) | 628.2 | 46.8 | | |

About 30 s of each row is PageDesk's own launch from floppy. The R6 question
says `about 21 minutes` on the XT and Esc cancels in 1.3 s (F23); one R6
password run measured 437 s analytically and 1.4x that on MartyPC, so the
estimate is 12.75 x (password length + 48) s a run. Ciphers on an 8088: RC4
23 KB/s, AES-128 3.4 KB/s, AES-256 2.6 KB/s.

**The 360 KB pair** (O7; MartyPC 5150, real disks): the program disk at
349 of 354 clusters (`pd360`), the README and samples on the second; with the
program disk out, an open says `Insert the PageDesk disk; open again.`, and
with it back the same open draws (F21).

## 7. Open items

Deferred in the fix round, each with what it would cost. The rule the owner
set for the last rounds (2026-11-10): a page drawn wrongly WITHOUT saying so
is fixed; everything else is written here. None of these is silent: each is a
slow path on the XT or a limit that writes its note.

| # | item | where it bites | cost and shape |
|---|---|---|---|
| OI-1 | the display list sized to the page, so W gets more than its 32 KiB minimum on 640 KB | a 20-40 KB embedded font subset (QUARTZ.PDF) is drawn in PageDesk's own face on a 640 KB XT ("font substituted"; PAGEDESK-SPEC 20.22.2) | rcore's side of the claim-B shrink; plat took the edits store at the first edit (the other half). Also F24: on the XT the decoders leave less than 32 KiB of heap reserve while a page draws (QA F24; PAGEDESK-SPEC 16.1 states it as a limit) |
| OI-2 | a resumable font-program inflate in PDXF | one embedded font load is a single 15-28 s verb on a 4.77 MHz XT, and the keyboard waits for it | PDXF ~150-250 of its 268 free bytes, plus PDREND re-issuing the same GLYPH after a busy answer: keep the text operator's string and glyph index across a slice (~0.5 KB) or rewind and skip drawn glyphs. PDREND has 18 bytes free, so something moves to PDRENDC first. Restarting the pass works today with no change, at the cost of re-interpreting up to the glyph |
| OI-3 | PDENG and disk-bound slices on the XT | an open's slices, the first read of a page stream and a module load from floppy take 4-7 s each | not measured per operation yet |
| OI-4 | an open with EVERY part fetch refused sits at "Opened X" for 150 s with no toast | artificial (plat forced it); no real machine refuses every fetch | the occupant ladder's give-up path |
| OI-5 | colour on VGA under transparency | a direct-ExtGState alpha or blend on a coloured mark is composed in the 8-bit grey band (O4) and loses its colour | a colour band; the owner chose 8-bit (O4). A transparency group is quantised to 7 grey levels (QA's group.pdf) |
| OI-6 | coloured tiling patterns of more than 40 tiles, and pattern strokes | drawn as a flat grey approximation, and black (PAGEDESK-SPEC 20.22.9) | PDXG has 18 bytes free |
| OI-7 | Find on a page not yet drawn, on 640 KB | answers "No match ... - draw page N first" because PDXF does not fit beside the overlay | PDXF's workspace minimum |
| OI-8 | the colour cache (XC) grant | a page that needs PDXF and PDXG in turn can lose its colour cache: QZ1035 draws either its photo or its chart's red series, and PDFKIT p1 can say "colour cache" while drawing right. It changes between runs with when the thumbnail job runs. Loud ("colour cache" / "colour space"), never silent | measured by plat (`~/Repos/pdany-logs/plat-fix-report.md`): (1) the thumbnail's ladder runs with the overlay on the stack, cannot drop it, and releases the page's XC (`sl_rel_xc`), and PDREND then marks the view XR_MEM for good; (2) the grant checks the 16 KiB increment plus 32 KiB, but X's regrow moves X and needs its whole 56 KiB in one run. The fix is two halves: PDREND re-asks a released or refused XC once the occupant is in (rcore, `pdrv.c`), and a refused XC/X8 grant frees the occupant next to X and retries (plat; `plat-fix/r29-xc-candidate.diff`). KBMIDI2 opened first on 640 KB is the same family: no X8 and no XC stash, so its 1,340-1,860 px Indexed images hit the raw path's 1,024-byte row (`image row size`) and a soft-masked image cannot be composed. Related, measured (QA R30): after a fresh open the heap is 23.5 KB unclaimed, not 87 KB, because PDXG (S 64.5 KB + W 13 KB) now loads where the 8-claims-per-owner limit used to refuse it silently. A floor could come back by (a) releasing S+W at File > Open and close, or (b) PDREND reporting whether a view used PDXG so the primary drops an unused occupant at the view's end |
| OI-9 | notes while thumbnails draw | the Notes panel says "Reading notes..." until the thumbnail job ends (BIG_OS: about 160 annotations) | eng lists notes between thumbnail slices (dc7db912c); the panel's own wait is the rest, primary bytes |
| OI-10 | Save As with a name that is not 8.3 | refused with `X is not a valid file name (8 letters, a dot, 3).`, but the form is not shown again to correct it (F30) | ui's overlay: re-open the form after the refusal |
| OI-11 | the Mark tool after one or two saves on a 640 KB machine | intermittently refused with `Out of memory. Close a window, retry.` (CFFCID, 2 opens of 6, 20 KB free); the lazy edit store gets nothing shed for it (F33) | plat: let the edit-store claim shed the occupant as the XC grant would (OI-8's half (b)) |
| OI-12 | a save refused for a full or write-protected disk | the refusal shows for 0.2-0.5 s and is then replaced by `2 changes - Unsaved` (F34); the marks are kept and nothing is written | ui/wrt: hold the refusal until input, as the SYNTH toast does |

## 8. Log

- 2026-11-09: goal; four scouts (platform, engine, render, writer); owner
  decisions O1-O7; ABI and contract (20.22) pinned; wave 1 launched.
