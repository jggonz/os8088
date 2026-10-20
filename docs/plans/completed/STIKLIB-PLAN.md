# STIKLIB-PLAN: one copy of the Sticky App library, shared by every Sticky App

**BUILT - SPEC.md 110.15 is the contract and this file is the design record
behind it.** The owner approved the ABI change on 2026-10-17 and section 4's
design was built as written: `apps/stiklib.asm`, `incbin`ned into DESK.DRV,
OSAPI_WM_DESK verb 4, the block at image offset 288, `apps/os88stikc.inc`.
Two things came out differently from the estimates below, both MEASURED in
SPEC.md 110.15: the library is **4,238** bytes (section 5 said ~4.5 KB), and
the per-package residue is the block's 464 mostly-zero bytes plus ~210 of
thunks, strings and bind - the state moved from bss into the image, so RAM
saw only the code leave. The header the poll and the save work on became a
copy in the block (`stik_hd`), which section 2.1 did not foresee.

Written 2026-10-17 on `main-size-pass` (base
`84157fc4`, after the shared-code size pass `2ec2298e`). It answers the
owner's question: can the code every Sticky App carries
(`apps/os88stik.inc` and the parts of `apps/os88gfx.inc` it turns on) be on
the disk ONCE, put on the heap by the first Sticky App that runs, used by
every other one, and freed when the last one closes?

**Verdict: yes, with no resident kernel byte, by riding `DESK.DRV`** (SPEC.md
11.103.10) - but it needs **an ABI change** (a fifth `OSAPI_WM_DESK` verb, and
the lib's own entry table, which is an ABI between two separately built
binaries), so it stops here for the owner. Every way that needs neither was
priced and fails on the lifetime (section 3). The win is real but narrower
than "three copies become one" sounds: **~5.9 KB of RAM with all three
Sticky Apps up, ~2.5 KB with two, and a LOSS of ~0.6 KB with one**, plus
**~5 KB off every kern_big system disk**; kern_small, the RAM-tight machine,
gains nothing (section 5).

Numbers marked MEASURED were taken on this branch with
`nasm -l` attribution by label and `tools/os88lz.py`'s LZ4. Everything else
is an ESTIMATE and says so.

## 1. What is being shared, MEASURED

The shared code in each package's image after the size pass (`2ec2298e`),
and what it costs on the disk - "alone" is the bytes compressed by
themselves, "marginal" is what the package file shrinks by without them:

| build | shared bytes in the image | packed alone | marginal in the `.o88` |
|---|---:|---:|---:|
| Desk Calendar, `STIK_DESKONLY` (kern_big) | 3,682 | 3,436 | 3,344 |
| Sticky Note, built `STIK_DESKONLY` | 3,427 | 3,193 | 3,044 |
| Desk Clock, built `STIK_DESKONLY` | 3,723 | 3,456 | 3,285 |
| Desk Calendar, `APP_SMALL` (kern_small) | 3,994 | 3,687 | 3,582 |

Code compresses poorly (7-11%), so the disk saving tracks the RAM saving
closely. The three kern_big arms together carry **10,832 bytes** of it in RAM
when all three run, and **9,673** on the system disk.

## 2. The two hard problems, and their answers

### 2.1 Reaching the caller's state

Today every routine addresses its state DS-relative at an offset that is
different in every package (`stik_file equ os88_image_end + STIK_BSS`), and
reads a dozen package constants as immediates (`STIK_FACE_W`, `STIK_TAG`'s
width and the plate geometry it implies, `STIK_RIGHT`, `STIK_PAYLEN`,
`GFXE_BAND_BUF` / `_ST` / `_H`, the layout table's address).

- **The state goes at a FIXED IMAGE OFFSET in every Sticky App**, right after
  the header and the icon (the code starts at +0x60 today). It is ~260 bytes
  of zeros: the v1 fields, the widget fields, the 128-byte row, the FIND
  buffer and the 13-byte name, plus `gfxe_*`'s image words. It moves from bss
  into the image, so **RAM is unchanged** and the packed file grows by a
  handful of bytes. With it, the library's ~200 state references stay what
  they are: `[disp16]`, DS = the package. A base register was the
  alternative and would touch every one of them.
- **The constants become a DESCRIPTOR** inside that block, filled by the
  package at assembly time from the same `STIK_*` defines it sets today:
  face width (the library always runs VARFACE's code), tag string and its
  pixel width, `STIK_RIGHT`, the layouts, the payload's address, length and
  version, the band's address, stride and height, the state file's name, the
  caption, the toasts, and the callbacks (below). The header file and the
  payload stay where they are, reached through the descriptor - only
  `stik_load` and `stik_save` touch them.
- **The band's inner loops do not slow down**: `[GFXE_BAND_BUF + di]` becomes
  `[bx + di]` with BX loaded once a glyph (BX is free in the straight path,
  and its EA is 2 clocks CHEAPER), and `add di, GFXE_BAND_ST` becomes
  `add di, bp`. ESTIMATE: the straight path is within a cycle a row of
  today's.

### 2.2 Calling across, both ways

- **Package to library**: a thunk per entry, in the package, keeping today's
  label so no package source changes - `stik_slabs: call far [stik_lib+n]` /
  `ret`, 5 bytes. The packages call 17 entries today (`stik_load`,
  `stik_made`, `stik_touch`, `stik_org`, `stik_ctop`, `stik_slabs`,
  `stik_redraw`, `stik_redrawr`, `stik_resize`, `stik_quit`, `stik_desk`,
  `stik_paint`, and `gfxe_text`, `_glyph`, `_bfill`, `_bits`, `_bclear`).
- **Kernel to library**: `OSAPI_WM_ONCLICK` and the rest take a NEAR proc in
  the caller's segment (SPEC.md 20.2), so the five callbacks `stik_made`
  installs (click, mouse-up, timer, close, wake) are package thunks too.
  SPEC.md 20.8 rule 5 is untouched: the kernel calls package procs with near
  `ret`s, and only the thunks far-call.
- **Library to package** (`STIK_PAINTCARD`, `_COMPOSE`, `_ONCARD`,
  `_ONCARDUP`, `_ONFOCUS`, `_ONTICK`, `_ONCLOSE`, `_SIZEUP`, `_PACK`,
  `_UNPACK`): one package trampoline, `stik_xcb: call [stik_xt]` / `retf`, and
  the library stores the descriptor's near offset in `[stik_xt]` then far-calls
  `DS:stik_xcb`. **The far pointer is built from DS at the call, never
  stored**: a package region is MOVABLE (SPEC.md 66.6.1.1), and a segment the
  package stamped into its own state would be SPEC.md 66's silent corruption.
- **DS is the package on every library instruction**, which is what keeps the
  OSAPI cells working: they know their caller by its DS (`mem_own` takes the
  caller's segment, SPEC.md 50.3), so a file call or a claim made from the
  library is the package's.

ESTIMATE of what stays in each package: ~22 thunks (110 bytes), the
trampoline and the descriptor (~60), the bind in the entry proc - QUERY, the
new verb, a version check, the far pointers (~60). **~230 bytes a package.**

## 3. The lifetime, which decides where the library lives

"The first one loads it, the last one frees it" needs SOMETHING that outlives
any one Sticky App. Every heap claim belongs to the instance that made it and
the kernel frees it when that instance dies (`OSAPI_MEM_CLAIM`, SPEC.md
50.3). Four places were priced:

| where | lifetime | verdict |
|---|---|---|
| a claim of the FIRST Sticky App, the others told its segment | dies with that one app, under the others | **REFUSED.** Nothing can move the claim to a survivor: no slot hands a claim to another owner, and the only way to make one for the survivor is to call `OSAPI_MEM_CLAIM` with DS = the survivor's segment from the closing app's code - SPEC.md 20.8's model forged, and the survivor's region may be COMPACTED by that very claim, being outside its own callback |
| each Sticky App's OWN claim, read from one `STIKLIB.BIN` in `STIKAPPS\` | its own | needs no kernel and no ABI, and **saves disk only**: ~5 KB of system disk, but each app still holds a copy, now rounded to a whole KB, and each launch reads one more file (ESTIMATE ~0.8 s on a 5150, ~2.4 s at a boot that relaunches all three). Recorded, not recommended |
| a new kernel module, `STIKLIB.DRV` | `mod_need`'s, until a drop rule | a `mod_tab` row, a `MODFP` slot block and a load path are **resident kernel bytes** (SPEC.md 2.8.1: 4 bytes of `.bss` an entry, plus the row) |
| **inside `DESK.DRV`** | from the first QUERY to the restart (SPEC.md 11.103.10) | **RECOMMENDED** - section 4 |

**`DESK.DRV` is the lifetime the feature asked for, already built.** Every
kern_big Sticky App is `STIK_DESKONLY` (Desk Calendar today; Sticky Note and
the Desk Clock in this branch), whose `stik_single` asks QUERY before any I/O
and refuses the launch without it - so wherever the library is NEEDED,
`DESK.DRV` is already loaded, held, pinned (`MEM_K_MOD`, claimed top-down)
and never freed under a live widget. An `OS88_F_DESK` package even gets it
from the loader's step 5, BEFORE its region is claimed, so the library
arrives with no read of its own.

What it does not give is "freed when the last one closes": `DESK.DRV` is held
until restart, and the library with it. SPEC.md 11.103.10 already argues why
that is right for widgets (session-long, relaunched at boot) and prices a drop
rule at ~43 resident bytes; that argument now carries ~4 KB instead of 1 KB.

## 4. The design

1. **`apps/stiklib.asm`** - the library, assembled ONCE as its own flat binary
   at org 0 with package conventions (DS = the caller, OSAPI far cells, no
   kernel symbol), from `os88stik.inc`'s and `os88gfx.inc`'s source behind a
   `STIK_LIB` arm: constants read from the descriptor, the union of every
   option (VARFACE, SCALE, bold, PACK, DMGREPLACE, COMPOSE...) switched by
   descriptor flags at run time. It begins with a version word and a jump
   table at fixed offsets.
2. **`DESK.DRV` carries it by `incbin`** at the end of `.modj`, paragraph
   aligned. It is not assembled with the kernel's conventions, so
   `tools/os88ovlchk.py` has nothing in it to object to, and the module stamp
   (SPEC.md 2.8.2) covers it: a library left over beside another kernel is
   refused with the image that carries it.
3. **`OSAPI_WM_DESK` verb 4, LIB**: out DX = `CS + (stiklib >> 4)` - the
   library's own paragraph, so it runs at its org 0 - and AX = its version.
   The verb lives in `dkd_verb`, IN THE IMAGE (`cmp al, 4` beside the three it
   has): **zero resident kernel bytes**. kern_small's cell is `mod_gone`
   already and refuses it.
4. **The include gets a third arm.** `STIK_DESKONLY` packages include
   `os88stik.inc` as today and get the thunks, the trampoline, the descriptor
   and the bind - no package source changes beyond the one line that puts the
   state block after the icon (section 2.1). The titled arm (kern_small, every
   `APP_SMALL` build) keeps the code INLINE, byte for byte as now; a macro
   layer (`STIK_K(x)` - an immediate inline, the descriptor in the library)
   keeps one source for both.
5. **The bind fails closed**: no QUERY, no verb 4, or the wrong version is the
   same refusal `No desk layer (DESK.DRV)` says today - the DESKONLY arm has no
   inline copy to fall back on.

## 5. What it buys and what it costs (ESTIMATES unless marked)

**The library's size.** The union of the measured arms is ~3,980 bytes (the
Desk Clock's 3,723 MEASURED plus ~250 for the scales, which it does not
carry); the descriptor
reads, the trampoline calls and the jump table add ~400-600 (ESTIMATE): **~4.5
KB**, so `DESK.DRV`'s claim goes **1 KB -> 5 KB** (it is 241 bytes now).

**RAM, kern_big**, the shared bytes MEASURED in section 1 less ~230 a package
left behind, against `DESK.DRV` growing by 4 KB:

Both columns leave out the 1 KB `DESK.DRV` claims today, except the first
row's:

| Sticky Apps running | today | shared | difference |
|---|---:|---:|---:|
| none, after one has run | 1 KB (`DESK.DRV`) | 5 KB | **+3 KB** |
| the Desk Calendar alone | 3,682 | 4,326 | **+0.6 KB** |
| Calendar + Note | 7,109 | 4,556 | **-2.5 KB** |
| all three | 10,832 | 4,786 | **-5.9 KB** |

A region is rounded to whole KB (`mem_bytes_kb_x`), so each package's figure
moves by 0-1 KB around these. The 128 KB floor machine runs kern_small, has no
`DESK.DRV`, and **gains nothing** - the win lands on the machines with the
most memory.

**Disk, every kern_big system disk** (STIKAPPS\ and `DESK.DRV` are both
there): three packages lose 9,673 bytes MEASURED (marginal), `DESK.DRV` gains
~4.2 KB packed and each package keeps ~0.2 KB: **~5 KB saved**. On
`os8088-360.img` that is ~10 of the 57 free 512-byte clusters MEASURED.

**Disk time.** `DESK.DRV` is read once a session, at the first Sticky App's
step 5, and grows from one sector to ~10: one or two `int 13h` calls, ~0.4-0.8
s on a 5150 (PERFORMANCE.md's ~400 ms a call), once. Each package's own read
shrinks by ~3.3 KB, which is the same order back at every launch.

**Time per call.** A thunk is a near call plus a far call and `retf`, ~25
clocks more than today's near call, and a callback into the package the same
again. The Desk Calendar's face (~190 glyphs, ~95 ms of composition, SPEC.md
5.12.8) pays ~1-2 ms; the Desk Clock's tick makes a handful of calls. Under 2%
of either.

**Work.** The include's ~3.7 KB rewritten behind a `STIK_LIB` arm with every
constant through `STIK_K`, a version and an entry table to keep, a third build
arm to test (inline small, inline-or-thunk big), and every `stik*` / `desk*`
soak row twice over. This is the cost that should decide it.

## 6. Decisions the owner holds

1. **The ABI change**: `OSAPI_WM_DESK` grows verb 4 (SDK text, `abilock
   --update` without a bump - a contract extended, SPEC.md 20.2.0.1), and the
   library's entry table becomes an interface between `DESK.DRV` and every
   Sticky App, versioned by its own word.
2. **`DESK.DRV`'s claim, 1 KB -> ~5 KB, held until restart** - the drop rule
   SPEC.md 11.103.10 priced at ~43 resident bytes is the way to give it back,
   and is a separate question.
3. **Is ~5.9 KB with three widgets up (and a ~0.6 KB loss with one) worth a
   third arm of the include?** The disk half (~5 KB of the 360 KB system disk)
   is the stronger argument, and the disk-only variant in section 3 buys it
   without the ABI change, at the cost of RAM and a read a launch.

## 7. What it must not break

- **SPEC.md 20.8 rule 6** (no relocation): the bind fills far pointers to the
  library and nothing inside any image is patched.
- **SPEC.md 66**: `DESK.DRV` stays PINNED. If HEAP-UNPIN-PLAN ever makes a
  `MEM_K_MOD` claim movable, every Sticky App's stored library pointer is the
  thing that breaks - re-ask verb 4 at every thunk, or keep this module pinned.
- **Hibernate**: the image carries `DESK.DRV`'s claim and its slots together
  (SPEC.md 11.103.10), so the packages' pointers stay true across a resume.
- **tests/desklayer.py** counts the calls between `stik_tagdraw`'s entry and
  its one `ret`; the library's copy keeps that shape, at an address the row
  must read out of `DESK.DRV` rather than the package.
