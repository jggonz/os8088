# Package loader, instances, compression

Reviewer area key: `loader`. Tree: `the-big-review` (cut from `elendilon`), 2026-11-08.
Byte figures marked MEASURED come from `build/kernel.map` (kern_big, this tree);
everything else is ESTIMATED from the instruction encodings.

## Scope read

| file | lines | read |
|---|---|---|
| `kernel/loader.inc` | 2,279 | all of it |
| `kernel/instance.inc` | 2,495 | all of it |
| `kernel/lz.inc` | 1,068 | all of it (both arms, `KHP_LZ` and not) |
| `kernel/lzfilt.inc` | 80 | all of it |
| `kernel/compress.inc` | 3,920 | ~1,500: the banner, `cmz_pack`, `cmz_probe`/`hash`/`ins`/`fill`/`bit`/`room`/`gamma`, `cmz_verb`'s two verbs (`cmz_compress`, `cmz_uncomp`, `cmz_upkg`, `cmz_ufree`), the split-set join's record loop, `cmz_pkg`/`cmz_cpre`/`cmz_hdr`. The join's prompt/undo plumbing skimmed |
| context | - | `memory.inc`'s `mem_free_rec_x`, `deskmod.inc`'s `desk_drop_x`, `wm.inc`'s `wm_ptr2idx`/`wm_show`, `assoc.inc`'s `assoc_run_x` around its `ld_run_name_x` call, `ui.inc`'s Task Manager verdict, `kernel.asm`'s `api_rn`, The Wire's `OSAPI_PKG_START` site |
| SPEC.md | - | §20.2 (header, entry contract), 20.2.0.1, 20.13.8 + 20.13.8.1, §21 whole, 21.4, 21.5, 21.5.4, §29 to 29.5 |
| plans | - | `ABI-BREAK-LIST.md` (all), `KERNEL-BYTE-QUEUE.md` (grep), `CODE-REVIEW-TWO-CYCLES-2026-11-01.md` (nothing in this area) |

Resident footprint (kern_big, MEASURED off the map): the loader's `.cold` span
`ld_slot_rec`..`osapi_pkg_rehome_x` is ~1,314 bytes (`ld_start` alone 316,
`ld_run_body_x` 193, `ld_pkg_byname` 81, `ld_unreserve` 39); the decoder
`lz_decomp_x`..`lzf_decomp` is ~667 bytes of `.cold` (LZB's arm ~156 of it);
`instance.inc`'s `.text` `inst_kinds`..`inst_launch_post` is ~1,643 bytes
including the three run-encoded icons and the kind table. The compressor is
none of this: it is `DISKOPS.DRV`'s (`.modl`), on demand.

## Verdict

This is one of the best-engineered corners of the kernel. The v3 model - a
package owns a segment at org 0, so the file IS the image - is the right model
for an 8088, and everything good here follows from it: a two-instruction
header fence shared with the driver loader, a re-check after the full read,
in-place decompression with no second buffer and no margin, a re-home that is
a base move rather than a relocation, and teardown by owner stamp rather than
by trusting what a package says. Resource attribution (the entry-proc stamp,
owner words on every claim, the W_SEG window sweep) is better than anything
Windows 3.x or AmigaOS did. The weaknesses are of one shape: the
hostile-input discipline the code advertises has three holes on the
COMPRESSED path - one of them (image smaller than its own clear prefix) lets a
crafted or corrupt `.O88` make the decoder write ~64KB past its region - and
several rules about the loader's own state (no nested start, BX is a window,
`[ld_said]` is consumed) are held by prose rather than by a compare. All of
them are cheap to close with no ABI break. The documentation is the weakest
part: SPEC §21 and §29 still describe the v2 package pool, the KB granule, a
task word in the kind table and an icon table that are all gone, and several
banners in the code repeat them.

## Strengths

1. **The file is the image** (`loader.inc:12-17`, SPEC §20.2). No relocation
   pass, no fixup table to bound, and a header peek that is the whole of what
   validation needs. It is what makes in-place expansion (`loader.inc:1009-1083`)
   and the re-home (`:1577-1793`, `mem_reown_x`) possible at all. Same model
   as a DOS `.COM`, with a header DOS never had.
2. **One header body for two loaders.** `ld_hdr_ok` (`:292-327`) is the
   package and driver prologue, version as the argument, with `LD_EVER` the one
   refusal that says why. A module deliberately does NOT share it (`:284-288`),
   which keeps §51.1's two independent gates.
3. **Carry fence on image + bss** (`ld_hdr_size`, `:380-403`) and a 32-bit
   size ladder decided on the high word first (`ld_check_hdr`, `:450-504`),
   shared with the re-home arm so a part's header gets the same bounds as a
   file's.
4. **Nothing a broken package says is believed on abort.** `.abort`
   (`:1509-1563`) sweeps windows by the W_SEG stamp (`.sweep`, `:1796-1805`),
   frees by owner word (slot and segment), releases XMS, and re-homes the
   package's own toast so its reason survives. That is the right shape and
   should be copied anywhere the kernel undoes a third party's work.
5. **The entry proc is an identified dispatch** (`:1318-1356`): stamped with
   `snd_inst` like every callback, so its sound grants, XMS blocks, toasts and
   file-API folder are attributed. `inst_caller` (`instance.inc:1022-1037`)
   spends a stamp only on the task that wrote it - a cheap, correct answer to
   "who is asking" under pre-emption.
6. **In-place expansion with a raw tail** (`lz.inc:24-45`). The encoder cuts at
   the first peak of produced-minus-consumed, so R = U - P is exactly enough;
   one claim, no window, admissible on 128KB. Both pointers bounded, and the
   `KHP_LZ` fast path makes every refusal the old code made at the same symbol
   (`lz.inc:546-596`). Recommend it as the pattern for any future decoder.
7. **The instance table's mechanics**: one pointer-bounded walk (`inst_next`,
   `:356-364`); the snapshot record IS the instance record, so the `cli` window
   is one `rep movsw` (`:2296-2365`) with a `%if` ladder holding the mirror;
   per-instance current directories made lazy and free when nothing moved
   (`inst_vol_enter`, `:1072-1095`) - better than DOS's machine-global cwd.
8. **The worker's stack class rides a byte that was always 0** (`LD_H_CLASS`,
   `loader.inc:114-126`; `inst_pkg_spawn`, `instance.inc:2093-2111`): an
   out-of-range index gets the largest class instead of a refusal. A model of
   how to add a header field without a format bump.
9. **The compressor is a module that sizes itself to the machine**
   (`compress.inc:31-38`): the window drops until the tables fit, so a 128KB
   machine runs the same encoder over less history. The Uncompress verb is
   more careful than the resident loader (`cmz_uncomp` refuses
   `prefix >= image`, `compress.inc` `.pkg` arm), which is the template for
   F1 below.

## Findings

Ranked. Ids are `loader-N`.

### loader-1 - HIGH, bug, now: a compressed package whose IMAGE is smaller than its clear prefix makes the decoder write ~64KB past the region

`ld_check_hdr` bounds the image to `>= LD_HDR_SIZE` (32) and below the file
only (`loader.inc:445-449`, `:499-504`); the icon rule ("image >= 96 with an
icon") is os88pkg's alone (SPEC §21: "the icon rule is enforced by os88pkg,
not re-checked"). Step 6a then computes the clear prefix off the flags - up to
320 with bits 0, 1, 5 and 7 (`:1018-1035`) - and does

```
    sub cx, bx                  ; the body's length       (:1066)
    sub dx, bx                  ; ...and what it must produce   (:1067)
```

with no carry test. `image = 64`, flags = icon | assoc | glyph | card |
compressed (prefix 320): DX = 64 - 320 = 0xFF00, so `lz_decomp_x` is told to
produce 65,280 bytes into ES = base + 20 paragraphs of a region `ld_alloc`
sized `roundup512(64 + bss)` = 512. The bound the decoder enforces is the one
it was handed, so a stream of one literal and one LZ4 match at offset 1,
length ~65,000 floods everything above the region - top-down placement makes
that the most recently launched resident package's code. The file needs fewer
than 64 bytes and a crafted first sector (cluster slack carries the rest).
SPEC 20.13.8.1 analyses only the FILE-shorter-than-prefix half and concludes
"the decoder's output bound keeps every write inside the region"; that is
true only while image > prefix. The image arm (`ld_pkg_start_x` ->
`.expand`, `:1934`) has the same hole, and that arm is fed network bytes by
The Wire. `cmz_uncomp` in the module already asks the right question
(`cmp dx, bx / jae .rbad`).

Recommendation: refuse `image <= prefix` and `file <= prefix` before the
subtracts (both `jbe` to an LD_EBAD exit that pops the `push ds` at `:1011`).
Better, together with loader-3: compute the prefix ONCE in `ld_check_hdr`
(it already holds the flags in DL), store it in a `.bss` word, test
`image > prefix`, `file > prefix` and the region fit there - where a refusal
owes no unreserve - and let 6a read the word instead of re-laddering.
Bytes: +14 `.cold` resident for the stand-alone fix; ~+18 for loader-1 and
loader-3 together (ESTIMATED).

### loader-2 - MEDIUM, bug, now: the disk-swap re-check does not bound the new header against the region it re-checks

Step 6a's re-check (`:1091-1093`) re-runs `ld_check_hdr` on the in-region
header, which REWRITES `[ld_img]`, `[ld_bss]` and `[ld_ent]` from it - but
`[ld_need]` (the claim) was sized off the PEEK's values (`ld_alloc`,
`:1167-1175`). The ladder still pins `image == file` on the plain arm, but
`bss` is free, and on the parts and compressed arms the image is too. A
header that changed between the peek and the read - a swapped floppy, or a
redirected volume (§62.9) whose host file was rewritten between the 512-byte
`FSV_READAT` and the full read - then has step 7's `rep stosb` (`:1303-1307`)
zero `img2 + bss2` bytes of a `roundup512(img1 + bss1)` region. The re-check
exists for exactly this case and catches every other field.

Recommendation: after the re-check, `cmp dx, [ld_need] / ja` an LD_EBAD exit:
`ld_hdr_size` leaves DX = image + bss and nothing after it in `ld_check_hdr`
writes DX (`:506-524`), so the operand is already there. It also closes the
image arm's time-of-check gap (a caller's buffer edited between the header
check and the copy). Bytes: +6 `.cold` (ESTIMATED).

### loader-3 - MEDIUM, bug (documented, not taken), now: the compressed read/copy can overrun the region by up to 512 bytes

`ld_where` (`:858-889`) places a compressed file at
`R = roundup512(image - file)` and the read is `roundup512(file)` there
(`:983-991`), the image arm's copy `file` bytes (`:1923-1933`). Two round-ups
can exceed `roundup512(image + bss)` by 512 when bss is short
(image 1,026, file 513, bss 0: read ends at 2,048 in a 1,536-byte region).
`os88pkg.py` and the module's `cmz_hdr` (`compress.inc` `cmz_hdr`) both
refuse such a file; the kernel does not. The loader's own comment at
`:876-881` says "this cannot overflow it" - which SPEC 20.13.8.1 contradicts.

Recommendation: the guard 20.13.8.1 already names,
`R + file <= roundup512(image + bss)` (equivalent to the round-up form because
R is a multiple of 512), in `ld_check_hdr`'s `.over` arm - folded with
loader-1. Bytes: ~+16 `.cold` alone, see loader-1 for the pair. Listed as:
SPEC 20.13.8.1 ("the guard belongs in the ladder ... and it is not taken
here"). New argument: loader-1 is the same missing ladder, and is not a
512-byte overrun but a 64KB one.

### loader-4 - MEDIUM, bug, now: the entry proc's returned BX is never validated on success

On CF = 0, step 9 hands BX straight to `test byte [bx+W_FLAGS+1]`
(`:1460`, kern_big), `inst_bind_win` (`:1468`) and `cw_wm_show` (`:1495`).
`inst_bind_win` calls `wm_ptr2idx`, whose `div cl` raises INT 0 when
`BX - wm_wins` exceeds 255 * WIN_SIZE (`wm.inc:7278-7283`) - BX = 0, "I have no
window", is exactly that case: on an 8088 the BIOS's dummy handler returns
past the `div` with a garbage index that the clamp then maps onto the LAST
window, whose `wm_owner` byte this record now steals; on a 286 running
kern_big the fault returns TO the `div` and the machine hangs. SPEC §21 step 9
claims "BX sanity-checked exactly as before: inside wm_wins, record-aligned";
that text describes the retired abort path, and §20.2 correctly says the
abort path does not consult BX at all - neither path checks it on success.
`inst_wchk` (`instance.inc:1981-1999`) exists for package-supplied window
pointers but needs an owner, which an unpublished record does not have.

Recommendation: at `.reg`, before the record is touched:
`bx - wm_wins < MAX_WIN * WIN_SIZE` (the `inst_wchk` range test) and
`[bx+W_SEG] == [ld_base]` (the stamp `.sweep` already trusts), else
`.abort` - which sweeps whatever the entry made. The W_SEG compare alone is
7 bytes and catches every realistic wrong value; with the range test ~17.
Bytes: +7..+17 `.cold` (ESTIMATED).

### loader-5 - MEDIUM, hazard, now: "an entry proc may not START a package" and "UI task only" are prose; the slot fences neither

SPEC 21.5.4: "`ld_start` holds the outer launch's `[ld_base]`, `[ld_img]`
and `[ld_status]` across the far call into it, and a second `ld_start` would
run on top of them." Only the DOCUMENT arm honours it (`ld_pkg_byname`,
`:2048-2049`, the posted form). The image arm (`ld_pkg_start_x`, `:1851`) and
the plain by-name arm (`ld_pkg_byname` `:1990-1991`, `jz ld_run_name_x`)
re-enter the pipeline unconditionally. A nested start overwrites `[ld_rec]`,
`[ld_base]`, `[ld_need]`, `[ld_lname]`, then zeroes `[ld_base]` at its own
step 9 (`:1497`) - the outer launch resumes and publishes I_SPTR = 0 on
whatever record `[ld_rec]` now names (an already-published one). Nor does
`api_rn` (`kernel.asm:4747-4765`) or the body test `[sch_cur]`, so a worker
that calls the slot (a §20.6 rule 7 violation, but a package's mistake) runs
the loader concurrently with the UI task's. Every other package-facing door
in this area refuses what it can check (`inst_wchk`, `inst_pkg_fence`,
`osapi_pkg_rehome_x`'s `ES == [ld_base]`).

Recommendation: `cmp byte [sch_cur], 0 / jne .bad` at the slot's head, and
`cmp word [ld_base], 0 / jne .bad` on the image arm and the no-document
by-name arm (the document arm keeps its post). Bytes: ~+15..+19 `.cold`
(ESTIMATED).

### loader-6 - MEDIUM, rethink/prior-art, now (additive): the loader cannot refuse a package that needs NEWER cells at the same `PKG_FMT`

SPEC 20.2.0.1 records it: "a NEW package calling a cell an OLD kernel does
not have, at the same format ... far-calls past the old table's end. The
LOADER still starts it ... A package copied onto an old machine by hand is
still unfenced." The Wire's cell fence (§92.2) is the only guard. On this
project the apps disks and the system disk are separate floppies, so "a newer
apps disk in an older machine" is the ordinary way it happens, not a corner.

Prior art: GEOS - the closest peer, an 8088 GUI - records a PROTOCOL
(major.minor) per imported library in the geode header and refuses a load
whose minor exceeds the running one; Windows 3.x's NE header carries an
expected-Windows-version word and resolves imports by ordinal so a missing
entry fails at load with a message; OS/2 1.x fails the load naming the
missing ordinal. os8088 has the major (`PKG_FMT`, equality) and no minor.

Recommendation: a one-byte API LEVEL in the header - the Wire already has a
cell-level table to number it from - compared `<=` the kernel's own in
`ld_check_hdr`, refusing with `LD_EVER` ("Wrong os8088 version", the one
verdict a user can act on). Header byte 31 is the candidate: the name field
is 16 NUL-padded bytes of which `inst_set_name_x` takes 15 (`instance.inc:1242`),
so byte 31 is read by no kernel path; `os88pkg.py` must then refuse a
16-character name and write the level. Old packages carry 0 there and pass,
so `PKG_FMT` does not move. It protects only kernels from this one on, which
is the argument for doing it now rather than later. Bytes: ~+8 `.cold`
(ESTIMATED), tool work in `os88pkg.py`/`os88api.inc`. Listed as: SPEC
20.2.0.1 "What it does not cover" (named, no remedy proposed there).

### loader-7 - LOW, bug, now: `[ld_said]` leaks out of a load nobody reports

`.abort` sets `[ld_said]` when the aborting entry's own toast is up
(`:1552-1557`, `:1565-1573`); `ld_say_status` (`:548-557`) and ui.inc's Task
Manager route (`ui.inc:2944`) consume it. `ldf_ld_pkg_start` (`:2085-2103`)
never calls `ld_say_status` - correctly, it reports in CF - and assoc_run_x's
hand-on arm skips it on purpose (`assoc.inc:1315-1316`). Either way the flag
stays 1, and the NEXT load that fails through a Disk window is silent:
`Load failed` / `Out of memory` swallowed for an unrelated file.

Recommendation, cheapest: every site that clears `[ld_painted]`
(`loader.inc:612`, `:2091`, `assoc.inc:1309`) clears the adjacent pair as one
word (+1 byte each), plus a clear on the Task Manager route (+5). The better
shape is to stop passing verdict flags through `.bss` at all: the pipeline
already answers AL = status; AH = {painted, said} bits would retire both
globals and every clear. Bytes: +8 `.cold` for the patch, ~0 for the rethink
(ESTIMATED).

### loader-8 - LOW, weakness (drifted twin), now: `ld_unreserve` is a second `mem_free_rec`, and it has already lost a line

`ld_unreserve` (`:230-262`, 39 bytes MEASURED) exists because "the record is
not published, so mem_free_rec cannot be used: it reads I_SPTR, which step 9
has not written yet" (`:216-219`). The two bodies free the same three things
(slot owner, segment owner, kern_mega's high pair - `memory.inc:4825-4861`),
and `mem_free_rec_x` additionally lifts a page floor the instance left
standing (`memory.inc:4826`, SPEC 50.6.6.2: "the kernel lifts it for a package
that never did"). An entry proc that sets a floor and returns CF = 1 leaves
it standing - on the UI task's stamp, which is every later package launch's
claim.

Recommendation: write `I_KIND = KIND_PKG` and `I_SPTR = [ld_base]` at step 5
(`ld_alloc`, after the claim; the record is unpublished, so no walker sees it),
keep them current in `ld_hi` and the re-home arm (one store each), and make
`ld_unreserve` = desk drop + `cw_xm_release_rec` + `mem_free_rec_x` +
`[ld_base]` = 0. Step 9's I_SPTR store then goes. Bytes: ~-3 kern_big,
~-20 kern_mega `.cold` (ESTIMATED), and one teardown body instead of two.

### loader-9 - LOW, bug, now: `DESK.DRV` stays loaded when a widget launch meets a full instance table

`ld_alloc` loads `DESK.DRV` (`desk_need`, `:1199-1200`) before the record
scan; when no record is free, `inst_alloc` answers CF = 1 and `jc .nomem`
(`:1214-1215`) skips `.nodrv`'s `desk_drop_x`. `desk_drop_x` cannot simply be
called there: it clears IF_DESK on `[di]`, and DI is one past the table on
that path (`deskmod.inc:121`). The module stays until the next close runs a
drop. Recommendation: on the desk arm, refuse before `desk_need` when the
top-down scan finds no free record (it already walks the table), or drop with
DI aimed at a known-free record. Bytes: ~+5 `.cold` (ESTIMATED).

### loader-10 - LOW, hazard (contract prose), now: `OSAPI_PKG_START`'s image must be PINNED across the call, and only the caller knows it

`ld_pkg_start_x` banks the source's segment (`[ld_msrc]`, `:1872-1873`)
because `ld_alloc` can compact the heap - which can MOVE that very source if it
is a movable claim, and nothing updates the banked copy. The Wire pins its
claim around the call (`apps/thewire/thewire.asm:3234-3236`, `wr_fpin`), so the
one caller is right, but SPEC 21.5 says only "ANY segment - it is COPIED".
Recommendation: one sentence in SPEC 21.5's contract ("a movable claim must be
pinned across the call; the loader's region claim may compact the heap").
0 bytes.

### loader-11 - LOW, perf (module), now: the compressor spends ~10-15% of its parse finding a gamma's top bit from the wrong end

`cmz_gamma` (`compress.inc:1375-1406`) searches from bit 15 DOWN with
`shr ax, cl` (8 + 4n clocks on an 8088) per step. The values it codes are
small - a match length, `(offset >> 8) + 2` with offset under 16KB - so a
typical call walks 13-14 steps, ~900-1,000 clocks, before emitting 2-4 bits.
Two calls a match at roughly one match per 8 input bytes is ~230 clocks a
byte against the banner's ~1,600 a byte. Searching UP (`shr ax, 1` until
zero, counting) is ~25 clocks a bit of the value. `cmz_bit` (~250 clocks a
bit through two calls and three CS-memory round trips) and the hash computed
twice per literal (`cmz_probe` and then `cmz_ins` via `cmz_fill`) are the
next two items. ESTIMATED from 8088 clock tables, not measured: take the
`lzcomp` row's MartyPC cycle count once if it decides anything. Bytes: ~0
(module, `.modl`), resident 0.

### loader-12 - LOW, consistency, now: three clear-prefix ladders, and the one the module points to does not exist

The prefix ladder is written out in step 6a (`loader.inc:1018-1035`), in
`cmz_cpre` (`compress.inc` `cmz_cpre`), and is implied by the Sticky Apps menu's
first-sector read. `cmz_cpre`'s banner calls itself "ld_cpre's own ladder
(loader.inc...)" - there is no `ld_cpre`. Folding the module's copy onto a
resident routine would cost resident bytes to save module ones (the §29.1.1.1
item-3 argument), so keep two, but make the loader's a named routine if
loader-1's combined fix moves it into `ld_check_hdr`, and fix the comment.
Bytes: 0.

### loader-13 - LOW, perf, record: bss zeroing, the image arm's copy and LZ4 literals are all byte strings

`rep stosb` over the bss (`:1307`, 10 clocks a byte on an 8088), `rep movsb`
for the image arm's copy (`:1932`, 17) and for LZ4 literal runs (`lz.inc:623`,
17). Word forms are 7 and 12.5 a byte. On a 20KB bss that is ~12 ms a launch
against ~400 ms per `int 13h`; on LZ4 literals (74% of a code stream's bytes,
SPEC 20.13.9) runs are short enough that the split's fixed cost eats most of
it. Recorded so nobody re-derives it; not recommended. Bytes: +4..+6 each.

### loader-14 - rethink, flag day (not recommended): instances do not share code

A second Calculator reads and holds the whole image again, because DS = CS
is the package contract (§20.1) and code and data share one segment. Windows
3.x, OS/2 and GEOS share code segments between instances (the DS/DGROUP is the
instance); Mac System 6/7 avoided the question with one instance per app.
Making os8088 share code is a DS != CS ABI (every package rebuilt, every
`[cs:x]` store re-examined) - not worth it on a machine that runs few copies
of anything. The cheap fraction is a disk read, not RAM: a header opt-in
("my image bytes are never written; my state is bss") would let a second
launch copy the first instance's region instead of reading and decoding the
file - ~0.2 s of `rep movsw` against seconds of floppy. Spare bits exist in
`LD_H_CLASS` (bits 0-5 hold a class index far below 64). Recorded; needs a
measurement of how often second instances are launched before it is worth
~25 bytes. Phase: record.

### loader-15 - rethink, record: the launch resolves an INDEX against the poster's cache, and that is the right choice

The by-name resolver is "the safer resolution" (`:727-732`), and posting a
name instead of an index would retire FIELD-NOTES 4's class for good. But
`dskw_stat` walks directory sectors and `loader_run_x`'s cache arm costs no
read in the common case (`:639-645`, measured 3 reads / 531 ms against 2 /
306). On a floppy XT a read is ~400 ms, so the index stays; the `.loud` arm is
the one place an index can still meet a different listing (a purged cache,
`:635-637`), and it is rare. Recorded so the rethink is not re-derived.

## Rethinks (summary)

- **loader-1 + loader-3 as one change**: the clear prefix computed once in
  `ld_check_hdr`, where every compressed-file bound (image > prefix,
  file > prefix, region fit) is asked before anything is claimed. Fixes a
  HIGH and a documented MEDIUM for ~18 resident bytes.
- **loader-8**: write the record's owner fields at step 5 and let
  `mem_free_rec` be the only teardown body.
- **loader-7**: verdict flags in AH instead of three `.bss` bytes with four
  consumers and three clear sites.
- **loader-6**: an API level beside the format number - the GEOS protocol
  model.
- **loader-14/15**: considered and declined, with the reasons.

## Prior art

- **DOS** (EXEC, MZ): reads the header, allocates the largest block, applies
  relocations, frees MCBs owned by the PSP at exit. os8088's owner words are
  the MCB-owner model done per claim and per slot, and it does better on
  validation (DOS trusts the MZ header's sizes) and on placement (top-down
  regions keep data claims contiguous; DOS fragments by design). DOS put
  decompression in the program (EXEPACK, PKLITE stubs); os8088's single kernel
  decoder is cheaper in disk bytes and safer, provided its bounds are complete
  (loader-1).
- **Windows 3.0/3.1** (NE): segment table, LOADONCALL/discardable code
  reloaded from the EXE, shared code between instances, `MakeProcInstance`
  thunks to set DS. On floppies the discard/reload model is what made Windows
  miserable (re-reads and disk prompts); os8088's whole-image-resident plus
  package-driven parts is the better trade for an XT. Windows' teardown left
  GDI/USER objects behind; os8088's sweep is far better. Windows does better
  on load-time API versioning (expected version, ordinal imports) - loader-6.
- **OS/2 1.x**: per-process resource ownership and load-time ordinal
  resolution with a named failure. Same lessons as Windows, enforced harder.
- **Mac System 5-7**: the Segment Loader (CODE resources, a jump table,
  `UnloadSeg`) is what §73.14's `ovl_*` and §20.12's parts are to os8088; the
  application heap zone freed whole at quit is the cleanest teardown of the
  era. The `SIZE` resource lets the Finder refuse with "needs N KB" before
  launch; os8088's `LD_ENOMEM` is less informative but the region is sized
  exactly, so the refusal is rarer. System 7's `dcmp` decompressors are the
  closest analogue to §20.13, with the decoder itself a resource; os8088
  cannot do that for LZB because a transparent read of a user-compressed file
  can happen on any disk (the module rule).
- **GEOS**: geodes with per-library protocol numbers checked at load
  (loader-6), resources owned by the geode and freed with it, code resources
  swappable. The protocol check is the one systemic thing worth copying.
- **AmigaOS**: `LoadSeg` with hunk relocation and scatter loading; no resource
  tracking at all, so a crashed program leaks everything. os8088 is strictly
  better here.

## Doc/code mismatches

| file:line | current text | what the code does | proposed text |
|---|---|---|---|
| `kernel/loader.inc:49` | "instance table or pool exhausted" | there is no pool: `.nomem` is `inst_alloc` full or `mem_claimp_hi_x` refusing (`:1214-1228`) | "instance table full, or no heap claim fits the region" |
| `kernel/loader.inc:295` | "6 = package, 7 = driver: the" | `PKG_FMT` is 10 (`:70`), `DRV_VER` 12 (`driver.inc:391`) | "PKG_FMT = package, DRV_VER = driver: the" |
| `kernel/loader.inc:290` | "Both callers are .cold and so is this" | three callers - `ld_check_hdr`, `drv_check`, the re-home arm (`:1602-1603`), as the banner's own "all three callers" says | "All three callers are .cold and so is this" |
| `kernel/loader.inc:587` | "load, relocate and launch the package" | v3 has no relocation (`:12-17`) | "load and launch the package" |
| `kernel/loader.inc:710` | "apply the reloc table; zero the bss (killing the table)" | the banner is v2's: the body takes SI -> a staged entry, not AX (`:688`); peeks via `dsk_peek_x` into LOW_SEG and validates in place; no pool, no reloc | "in: SI -> the staged directory entry; out: AL = LD_*. Steps (SPEC.md 21): the entry's type and size (ld_take); peek the first sector (dsk_peek_x) and validate it where it landed; size and reserve the record and region (ld_alloc); read the image - or the compressed file, high - and expand it; re-check the header in the region (disk-swap guard); then ld_start: zero the bss, call the entry, publish. Every failure gives the region back (ld_unreserve)." |
| `kernel/loader.inc:934` | "staged back into the kernel segment for ld_check_hdr to read." | `ld_check_hdr` reads ES:SI in LOW_SEG directly (`:958-962`) | "validated where it landed: ld_check_hdr reads through ES" |
| `kernel/loader.inc:881` | "and this cannot overflow it." | it can, by up to 512 bytes, for a file os88pkg did not write (SPEC 20.13.8.1) | "and this cannot overflow it FOR A FILE os88pkg WROTE - a hostile header can, by up to 512 bytes (SPEC.md 20.13.8.1)" (or delete once loader-3 lands) |
| `kernel/loader.inc:20` | "so the store that frees a record frees" | the region is freed by `mem_free_rec` at both teardown sites (`instance.inc:1724`, `:1944`), before the record store | "mem_free_rec releases every claim the slot and the segment own, region included, at both teardown sites" |
| `kernel/instance.inc:7` | "and the package-region allocator derives" | regions are heap claims (`ld_alloc`, `mem_claimp_hi_x`); nothing derives occupancy from the table | "and every package region is a heap claim owned by its record's slot (SPEC.md 20.1/50.3)" |
| `kernel/instance.inc:21` | "Once it has one, its region is freed by" | `inst_task_die` frees the region with `mem_free_rec` (`:1944`) and only then releases the record via `task_exit` | "Once it has one, its region is freed by mem_free_rec on the worker, in inst_task_die, and the record by task_exit's release byte" |
| `kernel/instance.inc:54` | "0xFFFF for a 64KB region (SPEC.md 20.1.1)" | I_SIZE = roundup512(image + bss) <= APP_MAX_SIZE = 0xFE00 (`loader.inc:1263-1269`) | "at most APP_MAX_SIZE (0xFE00, SPEC.md 20.1.1)" |
| `kernel/instance.inc:153` | "and 0xFFFF for a 64KB one (SPEC.md 20.1.1)" | as above | "at most 0xFE00 (SPEC.md 20.1.1)" |
| `apps/os88api.inc:2786` | "and 0xFFFF for a 64KB one (SPEC.md 20.1.1)" | as above (the SDK prose; fingerprinted by abilock as an indented contract line only if under a %define - check before editing) | "at most 0xFE00 (SPEC.md 20.1.1)" |
| `kernel/instance.inc:369` | "the first live KIND_PKG record" | `inst_next` returns dying (I_STATE 2) records too and `inst_of_seg` does not test I_STATE (`:386-396`) - and `inst_seg_parked` needs dying ones | "the first KIND_PKG record, live or dying," |
| `kernel/instance.inc:1445` | "AL = kind (0..6)" | kinds are 0..2, and 3 on kern_big (`:101-109`) | "AL = kind (KIND_ABOUT..KIND_CTRL, or KIND_HIBER on kern_big)" |
| `kernel/instance.inc:1446` | "(instance/window/task table full)" | no task is spawned (`:1612-1618`) | "(instance or window table full, or a singleton only dying)" |
| `kernel/instance.inc:1455` | "spawn the kind's task (rollback on a full" | no task arm since SPEC 14.8 (`:1612-1618`) | "...publish I_STATE=1 (LAST record write), then show the window." |
| `kernel/instance.inc:1646` | "is task_exit's release byte instead." | `inst_task_die` frees the region with `mem_free_rec` before `task_exit` (`:1944-1946`) | "is inst_task_die's mem_free_rec, on the worker, and the record goes with task_exit's release byte" |
| `kernel/instance.inc:1978` | "loader already refuses to wm_destroy an aborting entry's BX without exactly" | the loader never consults BX on abort; it sweeps by W_SEG (`loader.inc:1515-1523`); and `wm_ptr2idx` clamps the index now (`wm.inc:7295-7298`), so the `wm_owner` load is bounded - the `div` fault is what remains | "...and the `div cl` FAULTS once BX - wm_wins exceeds 255*WIN_SIZE. (wm_ptr2idx clamps the index it returns, so the table load is bounded; the fault is not.)" |
| `kernel/instance.inc:2036` | "relocated as class 0 by os88pkg.py" | v3 has no relocation; AX is a plain offset in the package's segment | "a near offset in the package's own segment (SPEC.md 20.1)" |
| `kernel/instance.inc:2140` | "which frees the record and with it the package" | the region is freed by `mem_free_rec` in `inst_task_die` before `task_exit` (`:1944`) | "mem_free_rec, which frees the region and every claim, then task_exit with BX = the record, which frees the record and the task slot inside one IF=0 window" |
| `kernel/instance.inc:2489` | "HERE before that and is now dsk_ico itself" | the staging slot is `inst_icobuf`, its own 64 bytes, precisely NOT `dsk_ico` (`:2415-2421`, `:1316-1321`) | "...was 64 bytes here once, then dsk_ico, and is inst_icobuf above since the worker-teardown race (its banner says why)" |
| `kernel/compress.inc:3603` | "os88pkg.py refuses it too, in the" | `os88pkg.py` packs a parted image since SPEC 20.13.8 (`compress_parted`); only this verb refuses | "...circular for THIS verb: os88pkg.py solves it as a fixed point (SPEC.md 20.13.8) and the machine does not attempt it" |
| `kernel/compress.inc:3640` | "ld_cpre's own ladder (loader.inc; SPEC.md 20.13.1)" | there is no `ld_cpre`; the ladder is inline in `ld_run_body_x` step 6a (`loader.inc:1018-1035`) | "the ladder ld_run_body_x's step 6a writes out (loader.inc; SPEC.md 20.13.1)" |
| `SPEC.md:44257` | "the loader copies it again into `inst_icons` (§29)" | no icon table: I_ICON = the I_ICO_PKG sentinel, staged by `inst_icon_ptr` when a tile is drawn (`loader.inc:1441-1457`) | "the loader records only that it is there (I_ICON = `I_ICO_PKG`, §29.1) and `inst_icon_ptr` stages it from the region when a tile is drawn" |
| `SPEC.md:50433` | "3 too large, 4 entry aborted, 5 out of memory)" | `LD_EVER` = 6 exists (`loader.inc:50`) | "3 too large, 4 entry aborted, 5 out of memory, 6 `LD_EVER` another format)" |
| `SPEC.md:50430` | "latches `ld_pwin` and calls `fmv_sync` on it **before** `ld_run_body`" | a QUIET chdir to the poster and one copy out of its own cache; `fmv_sync` only when the cache is gone or the chdir fails (`loader.inc:616-686`, LISTING-HOME-PLAN wave 1) | "stands where `ld_pwin` stands (a quiet chdir) and stages the entry out of that window's own cache - `fmv_sync` only when the cache was purged or the folder is gone" |
| `SPEC.md:50444` | "now a whole-KB multiple because that is" | 512-byte multiple: `mem_claimp_hi_x` in paragraphs, exactly the size asked (`loader.inc:1220-1225`) | "a multiple of 512, the claim being exactly what int 13h needs (§50.2.1)" |
| `SPEC.md:50447` | "No compaction — regions never move once loaded" | regions are movable under §66 (OS88_REGION_MOVABLE, `[ld_base]` pinned only during a load, `loader.inc:1497-1505`) | "Regions are allocated from the far end; once the instance is published a region may move under §66.6.1 like any declared claim, and `[ld_base]` pins the one being loaded" |
| `SPEC.md:50450` | "the first-fit-over-`inst_tab` allocator this replaced, is gone;" | `ld_alloc` exists - the name now means steps 4 and 5 (`loader.inc:1166`) | "the first-fit-over-`inst_tab` allocator this replaced, is gone, and the name now belongs to steps 4 and 5 (§21.5);" |
| `SPEC.md:50496` | "(16 — else status 3" | `PKG_FILE_HI` is 512, tested as DH >= 2 (`kernel.asm:87`, `loader.inc:797`) | "(512, compared as DH — else status 3" |
| `SPEC.md:50502` | "1..15 pass" | 1..511 pass; a parted package may be up to 32MB (`loader.inc:799`) | "1..511 pass, because a package carrying parts may be up to 32MB" |
| `SPEC.md:50538` | "AX = [ld_clus] → `dsk_clus2lba` (CF → status 2), then" | `dsk_peek_x`, one body for both transports; a cluster out of range is now LD_EDISK (`loader.inc:935-954`) | "`dsk_peek_x` (CF → status 1 - a cluster out of range included, which read as status 2 before the factoring), which is" |
| `SPEC.md:50564` | "`mem_claim_hi` need KB → CF →" | `mem_claimp_hi_x`, paragraphs (`loader.inc:1220-1228`) | "`mem_claimp_hi` need/16 paragraphs → CF →" |
| `SPEC.md:50571` | "ES = [ld_base], BX = 0" | a compressed file is read whole at R = roundup512(image - file) (`ld_where`, `loader.inc:858-889`) and expanded down (step 6a) | append: "— for a PLAIN image. A compressed one is read whole (`roundup512(file)`) at R = roundup512(image − file) and expanded downwards (§20.13); that read can exceed `need` by up to 512 (§20.13.8.1)" |
| `SPEC.md:50577` | "reading exactly ceil(image/512)·512 ≤" | true of the plain arm only | qualify as "the plain arm reads exactly..." |
| `SPEC.md:50583` | "8. Zero bss-size bytes at segment:image." | the code numbers bss as step 7, the launch 8 and the publish 9 (`loader.inc:1302`, `:1311`, `:1418`), and every code comment cites those numbers | renumber §21's 7-9 to the code's: "7. Zero bss ... 8. Far-call the entry ... 9. Register, publish, show", and note "(step 7 was relocation in v2)" |
| `SPEC.md:50592` | "sanity-checked exactly as before: inside wm_wins, record-aligned, then" | the abort path sweeps by W_SEG and never consults BX (`loader.inc:1515-1523`, matching §20.2); the success path does not check BX either (loader-4) | "CF → the abort path (BX is not consulted: every window the region's W_SEG stamps is destroyed, §20.2), status 4" |
| `SPEC.md:50634` | "that store frees the region" | `app_close_win` calls `mem_free_rec` before the store (`instance.inc:1724-1728`) | "`mem_free_rec` frees the region and every claim, then I_STATE ← 0 frees the record" (and the same for the worker path two lines on) |
| `SPEC.md:49073` | "the decoder's output" (... "bound keeps every write inside the region") | true only while image > prefix; with image < prefix the expected output wraps too (loader-1) | "...the decoder's output bound keeps every write inside the region **only while the image is longer than its prefix** - an image shorter than it wraps the expected output as well, and that is a 64KB write (fixed by ...)" |
| `SPEC.md:67667` | "(from format v2 on, §21) the package-region allocator" | regions are heap claims owned by the slot | "and every package region is a heap claim owned by its record's slot (§21)" |
| `SPEC.md:67678` | "app_launch for a built-in kind with KD_TASK" | no KD_TASK (§29.3's own text); only `inst_pkg_spawn` sets it | "by inst_pkg_spawn, for a package's one worker (§20.6) - no built-in kind has a task since §14.8" |
| `SPEC.md:67802` | "a 64KB region reads `0xFFFF` (§20.1.1)" | at most 0xFE00 | "at most `0xFE00` (§20.1.1)" |
| `SPEC.md:67849` | "no explicit region-free call — the store that frees the record frees" | the region is freed by `mem_free_rec` at both free points; rule 7 is v2's pool | "Package regions are heap claims owned by the record's slot: `mem_free_rec` frees them at both free points, before the record store" |
| `SPEC.md:67892` | "one 16-byte row per kind; the" | 14-byte rows (`KD_SIZE`, `instance.inc:188`); the CP row has an init proc and an icon; Hibernate is the last row on kern_big; the code sample still has the task column | "one KD_SIZE (14-byte) row per kind" and replace the sample with `dw cp_tpl, 0, 0, cp_kinit, cp_sname / dw inst_ico_ctrl / db 1, 0` |
| `SPEC.md:67881` | "Files 4 (stride" | the cap is `VIEW_SLOTS+1` = 5: four user windows plus the Standard File chooser's (`instance.inc:203-209`) | "Files `VIEW_SLOTS`+1 = 5 (four of the user's and the chooser's, §38.1; stride" |
| `SPEC.md:67918` | "template copied to scratch with x/y cascaded" | cascaded in place in the template and put back (`instance.inc:1557-1566`); "task table full" is gone | "template's x/y cascaded +16·s IN PLACE around wm_create" and drop "task" from the failure list |
| `SPEC.md:67925` / `SPEC.md:45968` | "I_SPTR ≤ AX < I_SPTR + I_SIZE" | I_SPTR is a segment and AX an offset: the fence is ES == I_SPTR and AX < I_SIZE (`instance.inc:2022-2027`) | "the caller's segment (ES) is I_SPTR and AX < I_SIZE" |
| `SPEC.md:67923` | "record-aligned, CF=1 otherwise. Preserves everything but the flags." | also resolves the owner and requires it live, answering DI = the record (`instance.inc:1966-1999`) | "...record-aligned AND owned by a live (I_STATE 1) instance, with DI = that record; CF=1 otherwise. Clobbers DI and the flags." |
| `SPEC.md:67979` | "plus module scratch (template copy buffer, pool-slot" | `inst_launch` is a byte; the template buffer is gone and the pool slot is DX (`instance.inc:2481-2486`); the side tables `inst_fdrv`/`fcwd`/`fpick`/`fname`, `inst_icobuf` and (OS88_COMPACT) `inst_parkreq`/`parksafe`/`restart`/`sch_parked` are not listed; `inst_init` zeroes only `inst_tab` and `inst_launch` | rewrite 29.5 as the list in `instance.inc:2401-2492` |

## Open questions

1. **Is `.bss` zero at boot?** `kernel.asm:5894-5899` and SPEC §21 (50422) and
   §30 (68154) say yes and the loader leans on it (`[ld_said]`, `[ld_rehome]`
   are never initialised); `instance.inc:1438-1441` says no and puts
   `inst_lkhd` in `.text` with an initialiser for that reason, as do SPEC
   54409 and 135169. One of the two camps is stale; whichever it is, the other
   camp's comments should be fixed, and if `.bss` is NOT zero then the loader's
   two bytes are latent bugs.
2. **Does any package return BX = 0 from its entry on success?** loader-4's
   fix turns that into a refusal; a grep of every entry proc's exit before it
   lands would settle whether it is a behaviour change for anything shipped.
3. **kern_mega and loader-1**: a region there is a selector with a limit, so
   the 64KB write may #GP inside the decoder rather than corrupt - which turns
   it into a kernel fault on a hostile file instead. Worth one Bochs run with a
   crafted header once the fix is written, to see which.
4. **Header byte 31 for loader-6**: confirm no shipped package has a
   16-character name (os88pkg would have written a non-zero byte 31).

## Verification

The adversarial verifier re-read the cited code in every build arm the
findings touch: `KERN_BIG`/`KERN_SMALL`/`KERN_MEGA` in `ld_where`, step 6a and
`ld_unreserve`; `lz.inc`'s bounds; `compress.inc`'s `cmz_uncomp`/`cmz_hdr`;
`instance.inc`'s teardown sites; `wm_ptr2idx`; `desk_drop_x`; and
`build/kernel.map` for `wm_wins`. Nothing here is already in
`docs/plans/ABI-BREAK-LIST.md` or `docs/plans/KERNEL-BYTE-QUEUE.md`; the
byte queue's `ld_unreserve` row is only about BP preservation. Machine-readable
verdicts and the 55 confirmed doc fixes are in `../data/loader.json`.

- **loader-1** - CONFIRMED (high). Step 6a's `sub dx, bx` wraps when image < prefix, and `lz_take` bounds writes only by that count. The image arm shares the hole. SPEC 20.13.8.1's "the output bound keeps every write inside the region" is wrong and is corrected.
- **loader-2** - CONFIRMED (medium). On a plain image the re-check pins image to the file but not bss, so step 7 can zero past `need`. +6 holds if the new branch reuses the `mov al, LD_EBAD` above `.ret2j`.
- **loader-3** - CONFIRMED (medium). Up to 512 bytes on the disk read and 511 on the image-arm copy. SPEC 20.13.8.1 says 511 against its own 512-byte example; corrected.
- **loader-4** - CONFIRMED (medium). With `wm_wins` = 9FCAh, BX = 0 overflows the `div` and no INT 0 handler is installed. The W_SEG compare needs checking against kern_mega selectors and the re-home arm. Estimate is ~+8..+18.
- **loader-5** - CONFIRMED (medium). Only the document arm tests `[ld_base]`. The `sch_cur` fence is defence in depth, since a worker already may not touch files. Estimate is ~+15..+21.
- **loader-6** - CORRECTED. The gap is real, but `os88pkg.py` already refuses 16-character names, so byte 31 is always 0 and the tool change is only writing the level. Check the name field's other readers first.
- **loader-7** - CONFIRMED (low). Neither `ldf_ld_pkg_start` nor assoc's hand-on arm consumes `[ld_said]`.
- **loader-8** - CONFIRMED (low). `ld_unreserve` lacks `mem_free_rec_x`'s floor lift. The byte estimates were not verified.
- **loader-9** - CONFIRMED (low). `jc .nomem` skips the drop, and DI is one past the table on that path.
- **loader-10** - CONFIRMED (low). The sentence for SPEC 21.5 is among the doc fixes.
- **loader-11** - CONFIRMED (spot-check).
- **loader-12** - CONFIRMED. `ld_cpre` does not exist; the comment in `compress.inc` and SPEC 54854 are both fixed.
- **loader-13** - CONFIRMED (spot-check).
- **loader-14** - unchecked (record item).
- **loader-15** - CONFIRMED (spot-check of the measured trade).

Doc mismatches: every quoted text exists exactly and once (SPEC's
`I_SPTR ≤ AX < I_SPTR + I_SIZE` occurs twice and both copies are fixed). All
are confirmed. One fix was reshaped rather than taken as written. Renumbering
SPEC §21's steps 7-9 to the code's numbering would break about twenty
`§21 step N` citations, so the fix adds a note to the retired step 7 that
states the code's numbering. Fixes added by the verifier:

- SPEC 20.13.8.1's 511 becomes 512.
- Both stale region-free clauses in §29.4's `app_close_win` row.
- SPEC 54854's `ld_cpre`.
- loader-10's pin sentence in §21.5.

No fix was refuted.
