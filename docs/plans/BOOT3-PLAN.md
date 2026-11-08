# `.boot3` — boot-only code that rides in KERNEL.SYS

> **Status: BEING BUILT. Waves 0, 1, 2, 3 and 4 are built (2026-11-08, sections
> 16.4, 16.5, 16.7, 16.8 and 16.10), and so are item E with the big review's
> canary fix (BR-1) and boot-7 (section 16.6), and wave 4's two preconditions
> boot-4 and boot-10 (section 16.9); wave 5 is not.** Written 2026-11-08 at the owner's
> request, after the big review's size ledger asked for a 14th blob sector
> and the owner refused it (docs/reports/BIG-REVIEW-2026-11-08.md section 7,
> docs/reports/big-review-2026-11-08/synth/phase1.md "Both boot-time homes
> are full on kern_big"). Three designers worked independently, from a
> minimal angle, a re-homing angle and a risk angle. This plan takes the
> minimal design as its base. It adds the risk design's fences and derived
> lift, and the re-homing design's end state and per-arm placement. Where
> they disagreed, the code decided (section 2). Figures marked MEASURED come
> from this tree's `build/kernel.map`, `build/smallk/`, `build/megak/` and
> `build/*kernel.kz.json`. PREDICTED figures are arithmetic and have not been
> booted. An adversarial verifier then checked it against the code the same
> day. Its corrections are made in place, and section 16.1 lists what it
> checked and what it changed.
>
> **Revised the same day for the owner's second round**
> (docs/reports/big-review-2026-11-08/owner-decisions.md, *Second round*):
> the section is GENERIC across the four kernels, wave 4 is part of the plan,
> the keyboard mouse's pointer half rides it as wave 5, `blobruns` moves to
> the fast tier, and the earlier bar without wave 4 is item E. The answered
> questions are folded into the body, each marked "(owner, 2026-11-08)".
> Section 16.2 lists what the revision measured and derived, and section
> 16.3 what a second adversarial pass checked and corrected.

## The owner's words

> "14th boot blob sector: It increases the time before the user sees the
> progress bar by 0.4-0.8 seconds because we get 1-2 more int13h's, so its no
> go. That said, I think we can solve this another way, so set an agent on a
> design for this. Right now we have .ovl, which hides inside the fat sector
> cache and a few .lowbss items that we put right next to it. Then we have
> .boot2; the thing .boot loads that drives the splash, and coincidentally
> also holds more stuff we can drop. Lets consider a .boot3 (or pick a better
> name), which is part of the kernel.sys file - the last thing in it, which
> can be dropped right before we start dropping the .vgabuf and other .vga
> stuff (on a non-vga system) and compacting in the heap and .cold. Aka,
> something we can afford to grow more, because it is read along with the
> kernel and the splash screen is already up so we're not delaying it
> longer. We could move stuff from .boot2 into it too, when its not related
> to 'drawing the splash screen and loading kernel.sys'. That takes .boot2
> back to its original purpose, too."

From the same message, two things bind the fences: *"the only thing worth
defending against is our own mistakes ... better to defend against those by
finding the issues and guarding them at build time rather than spending
resident bytes."*

The second round, the same day, as relayed and recorded in owner-decisions.md:

> On the kernels: "it buys them all space, and the same boot ladder which
> means we make less mistakes that affect another kernel. Unless small truly
> doesn't need it." On the name: "whatever is going to confuse a new session,
> looking at how we boot, the least." On wave 4: "why keep something bigger
> than it needs to be? The quicker splash is a benefit." On `blobruns`: the
> fast tier "if it's REALLY quick" (or full). On the earlier bar without
> wave 4: an option only "if it is less complex than wave 4".

And the keyboard mouse, the owner's own shape: its pointer half rides
`.boot3`; before the section is dropped, when no mouse has been found, it is
copied into a heap claim; and that claim is dropped when a mouse first speaks.
No disk read, no file on any disk, no system-disk dependency (wave 5).

## 1. The name: `.boot3` (owner, 2026-11-08: the least confusing to a new session)

The name is kept. The test is the owner's: what a new session, reading how
this OS boots, will misread least.

- **It is the reading order the file names already teach.** `boot/boot.asm`
  (stage 1) reads `.boot2` (`boot/boot2.asm`, the blob). `.boot2` reads
  KERNEL.SYS, whose last section is `.boot3`. "N is what N−1 loads" is the
  owner's own description, and a session that has read the first two needs
  no new idea for the third.
- **It names a lifetime, not a job.** Everything in it is dead at
  `mem_unblob_x`. That is the one fact a caller must know before calling in,
  and the name says it. `.init` or `.kinit` say what the code does, and a
  session would reasonably call an "init" routine from anywhere at any time.
- **It is unique.** `grep -w boot3` finds this plan and the review today,
  nothing in code. `.init` collides with more than a hundred `*_init`
  routines, most of them (`menu_init`, `inst_init`, `dock_init`) in `.ovl`;
  `kinit` already occurs in three files; `.btail` collides with KZIP's
  "tail", the whole packed remainder (SPEC.md 2.9.13.1), of which this
  section is only the last part; `.late` says nothing about what it is late
  relative to.
- **In the end state (wave 4) its entry point is `kmain_o`**, the kernel's
  one-time bring-up.

**The one trap, and how it is closed.** A session that has matched `.boot2`
to `boot/boot2.asm` will look for `boot/boot3.asm`, and `.boot3` is not a
loader stage: nothing jumps to it, it has no entry of its own, and its bodies
stay in the modules that own them (`font.inc`, `hiber.inc`, `driver.inc`,
`mouse.inc`). So:

- SPEC and the code call it **"the third boot-only home"**, never "stage 3".
- Its frame gets one file, **`kernel/boot3.inc`**, `%include`d where today's
  `.cfx` block sits at kernel.asm's foot. It holds `cfx_slide`, `cfx_grp`
  and `cfx_tab` (moved, names kept), the keyboard mouse's adoption door
  (wave 5) and the macros' notes, under a header that is section 3.2's
  lifetime table. `ls boot/ kernel/` then finds it, and `grep boot[23]`
  finds every boot-only body in the tree.

**On kern_big and kern_emu the section takes over `.cfx`; it is not a new
one.** `.cfx` is already a `.boot3`: 530 bytes (MEASURED, 0x191A0..0x193B2 in
the file). It is the last section of KERNEL.SYS, expanded by stage 2 into RAM
nothing claims until `mem_unblob_x`, and three blob bodies already far-call
it (`call CFX_RAM:cfx_grid/cfx_ctxcap/cfx_cfgname`). The section is renamed;
its labels keep their names (`cfx_slide`, `cfx_grp`, `cfx_tab`), and so does
`tools/os88cfx.py`. kern_small and kern_mega have no `.cfx` (MEASURED: neither
map has the section), so there it is new.

Names used below:

| name | what it is |
|---|---|
| `B3_START` | the section's start in the file |
| `B3_RAM` = `B3_SEG` | its constant segment |
| `B3_SIZE` | its length |
| `.boot3b` | its nobits scratch |
| `B3_RES` | the length of `.boot3b` |
| `.boot3m` | the keyboard mouse's module image, the section after `.boot3` (wave 5) |
| `B3CALL`, `B3SECT`, `B2CALL` | the macros (section 4) |

## 2. What the code settled between the three designs

| question | answer | from the code |
|---|---|---|
| Room under the blob today | **kern_big 1,024 bytes**, not 768; kern_small 1,536; kern_mega 2,048 | kern_big's packed read ends at 0x600 + 4,608 + R 18,432 + 145 × 512 = **0x18200**; the blob is at BLOB_SEG_AT = 0x1800 + 96 = **0x18600** (kernel.kz.json; kernel.asm BLOB_LIFT). The 768 figure mixed a kernel-relative end with a linear blob address. kern_small: 0x600 + 4,608 + R 10,240 + 86 × 512 = **0xEC00**, which is its KERN_END, against the blob at 0xF200. kern_mega: 0x600 + 4,608 + R 24,064 + 204 × 512 = **0x20E00**, against the blob at 0x21600 (all from each tree's kernel.kz.json and map) |
| Generic, or kern_big only? | **Generic (owner, 2026-11-08)**: every kernel, each switching on in the first wave that moves a body there | section 5 |
| Does kern_small truly not need it? | **It does not need the room, and it gains three things the room does not measure** (section 5.1), so it opts in | its `.boot2` has 702 free and its `.ovl` 62 (MEASURED), enough for its wants; the gains are wave 5's resident bytes against the 64KB rule, one ladder, and wave 4's shorter blob |
| Is `ovl_sc_*` a closed cluster? | **No.** It writes `ovc_buf` through CS: desksc.inc's `ovl_sc_load` does `push cs / pop es / mov di, ovc_buf / rep movsb` | moved alone, CS is `B3_SEG`, and the copy lands at `B3_SEG:ovc_buf` = 0x17DA0 + 0x1A00 = **0x197A0, inside the running blob**. So it moves in wave 2 with `ovc_buf` and the `ovc_*` family, not in wave 1 |
| Ban COLD_SEG in `.boot3`, hop through `[api_coldseg]`, or re-point a fourth group? | **A fourth group** (kern_big, kern_emu), the ROM's `rt_b3` (kern_big, kern_small), nothing (kern_mega) | Every useful closed cluster names COLD_SEG. Decoding `cfx_tab` (MEASURED): 93 sites in `.text`, 3 in `.ovl`, **25 in the blob**, 192 of 256 bytes used. Wave 1 alone has 11. A ban moves nothing worth having. A hop costs 56 bytes of pair plus dual-arm macros at about 17 sites in shared source. The group costs 6 transient bytes and about 13 ROM bytes. kern_mega's COLD_SEG is a selector-compatible constant (kernel.asm's LADDER_PACK note: "COLD_SEG is a selector there, a multiple of 8 paragraphs") and nothing moves `.cold` before the drop |
| Fold `.cfx`, or a separate section at HEAP_SEG? | **Fold** (kern_big, kern_emu) | The head constraint already exists and is guarded: `%if cfx_grid - $$ < KVGABUF_SIZE`. The slide prefix (`cfx_slide` 71 + `cfx_grp` 26 + `cfx_tab` 256 = **353**, MEASURED) more than covers `.vgabuf`'s **336**. One section means one MODC_START, one tool classification, and no 78-byte gap |
| Guard 5's headroom once BLOB_LIFT is counted | kern_big **22,016 bytes** (43 sectors), not 22,272; kern_small **60,928** | 128 × 1,024 − 2,560 − BLOB_SEG_AT × 16 − BOOT2_PAD: 0x18600 and 6,656 on kern_big, 0xF200 and 5,632 on kern_small |
| Merge `.ovl` into `.boot3`? | **No.** | `.ovl` lands on the FAT window and `dsk_secbuf`, so it is the one transient that costs no boot RAM, and per byte it costs the same read. It sheds bodies (wave 3); it does not move |
| Does `bvs_apply` move? | **No.** | splash.inc:525-528 runs `BLOBCALL vid_detect` and `call bvs_apply` in the first tick, which is before `.boot3` has been expanded |

## 3. Where it sits, at each moment of the boot

### 3.1 On disk and in the packed stream

`.boot3` is KERNEL.SYS's last section on every kernel (with `.boot3m` after
it from wave 5): `MODC_START equ B3_START + B3_SIZE` (+ `.boot3m`'s size).
That replaces the `.cfx` arm at kernel.asm:8981 on kern_big and kern_emu,
the ROM_COLD arm (`COLD_START + COLD_SIZE`) on kern_small, and the
`MEGA_START + MEGA_SIZE` arm on kern_mega. With the section last:

- `B2_KSECS` (NOKZIP) and `KZ_SECS` cover it with no loader change.
- **It is LZ4-packed** as the end of the last block. Room before a new block
  (MEASURED, kz.json): kern_big's block 2 is 30,642 of KZ_BLK 61,440
  unpacked, about 30KB; kern_small's one block is 54,119, about 7.1KB;
  kern_mega's block 3 is 5,136, about 55KB.
- `os88kz.py` recomputes R.
- `kz_all`, `kz_expand`, `kz_hd` and the plain head (KZ_HEADSEC 9) are
  unchanged.

**The two-pass identity check (SPEC.md 2.9.13.4) constrains the contents.**
`.boot3` may not assemble any `-DKZ_*` number, or any blob label at or past
`boot2_entry`: the loader's offsets move between the passes, and the check
refuses that loudly.

### 3.2 In RAM

| kernel | `B3_RAM` | what it covers, in order | blob at |
|---|---|---|---|
| kern_big, kern_emu | `VGABUF_RAM`, today's `CFX_RAM`, 0x17DA (linear **0x17DA0**): `B3_START equ (COLD_START + COLD_SIZE + 15) & ~15`, `B3_SEG equ KERNEL_SEG + (B3_START - BOOT2_PAD) / 16` | `.vgabuf`'s rung (336 bytes); the LADDER_PACK rounding up to KERN_END (0x18000); the blob's lift (0x18000..0x18600) | 0x18600; with `ovc_buf` to 0x1A200 |
| kern_small | `.cold`'s end, to the paragraph: 0xEB7 (linear **0xEB70**; `.cold` ends at 0xEB67, MEASURED from the map: file 0xA940 + 0x5227 at COLD_RAM 0x994) | KERN_END's rounding (0xEB70..0xEC00, 144 bytes); the lift (0xEC00..0xF200). There is no `.vgabuf` (VGABUF_PARA 0: no planar decoder on kern_small) and no slide | 0xF200 |
| kern_mega | **KERN_END**, 0x2100 (linear **0x21000**), behind a zero gap in the file from `.mega`'s end (0x20C10) that packs to almost nothing | the lift (0x21000..0x21600). It sits past `.mega`'s park scratch and `.vgabuf` rather than over them, so no `b3_live` rule applies there | 0x21600 |

The lifetime is the same on all four:

| moment | what is in `.boot3`'s RAM |
|---|---|
| stage 1, the splash's first tick, the tail read | packed bytes, or nothing yet. **Not callable** |
| `kz_all` returns (end of stage 2, after the canary) | `.boot3`, expanded and aboard |
| kmain → `SPLGATE1 kmain_o` → every kmain_o call | live. `mem_init_x` raises `[mem_base]` past the blob + BOOT2_RES (memory.inc:622-631), so no claim, read-ahead, DIRTYRAM fill or DSK_HCYLSEG buffer can land on it. The one deliberate claim is wave 5's adoption, which lands at the TOP of the heap and copies out of `.boot3m` before the drop |
| kmain zeroes `[spl_fseg]`, then `mem_unblob_x` | **dropped**: `mov [mem_base], ax` (memory.inc:686) gives back `.boot3`, the lift, the blob and, on a mono kern_big, the `.vgabuf`/`.vga` rungs, in one store. Then `mem_compact` runs |
| kern_mega's `mega_enter` (kernel.asm:6204) | after the drop: every `.boot3` body is real mode, and no selector ever names it |

That store is the owner's "dropped right before we start dropping the
.vgabuf and other .vga stuff ... and compacting". It is one store, and it
costs **0 new instructions** on every kernel.

**`.vgabuf` overlap (kern_big and kern_emu only).** On a VGA or EGA machine
at home, the planar decoder owns `.boot3`'s first 336 bytes during kmain_o.
The `cfx_grid` comment says the decoder is used before `cfx_grid` runs. So
only the slide prefix may live there, and it runs only on a machine with
neither a VGA nor an EGA. Every other entry follows a `b3_live` label,
asserted `>= KVGABUF_SIZE`. On a ROM-adopted machine, `rp_walk` has already
moved `.vgabuf` to COLD_RAM at kmain_o's start, so the overlap does not arise
there. On a COLDHMA.SYS machine the same move happens only inside
`drv_boot_x`, so until then the overlap stands exactly as at home; the
`b3_live` rule covers both. kern_small has no `.vgabuf`; kern_mega's `.boot3`
starts above it.

### 3.3 The lift is derived, not hand-set

Today `BLOB_LIFT equ 96` (kernel.asm:1952, under `%ifdef ROM_COLD`) is a
guess, and the guard at boot2.asm:546 checks it on pass 2. As `.boot3`
grows, each wave would fight that `%error`. Instead, on every kernel whose
`.boot3` is switched on (until then kern_small and kern_mega keep today's
literal 96 and stay byte-identical), with every term in BYTES of linear
address and the result in paragraphs:

```
CEIL512(x)  = (x + 511) & ~511
B3_END      = B3_RAM*16 + B3_SIZE (+ .boot3m's size from wave 5)
BLOB_LIFT   = max(96, CEIL512(B3_END + KZ_MARGIN_K + 1022)/16 - HEAP_SEG,
                      CEIL512(B3_END + B3_RES)/16          - HEAP_SEG)
```

CEIL512 keeps the blob 512-aligned (it is a read target), and HEAP_SEG is a
multiple of 32 paragraphs already (KERN_END's rounding). Checked against
today's kern_big map: B3_END = 0x17FB2, + 1,086 = 0x183F0, CEIL512 =
0x18400, so the bound gives 0x1840 - 0x1800 = 64 paragraphs and the floor of
96 wins.

**What fits under the floor of 96**, the certain bound
`blob - 1,086 - B3_RAM*16` (PREDICTED arithmetic on MEASURED addresses):
kern_big 1,058 bytes (of which `.cfx` is 530 today), kern_small **594**,
kern_mega **450**. Past that, each 512 bytes of `.boot3` costs 32 paragraphs
of boot RAM, never a resident byte.

- The 1,022 is the worst case of R's sector rounding plus the tail's.
  os88kz.py's `r = (tail - body + margin + extra + 511) & ~511` and the
  tail's own read of `ceil(body/512)` sectors are the two 511s. `extra` is 0
  on every kernel whose blocks all shrink; a last block that grows (it has
  happened, on MEGATRAP=1's kern_mega) adds to R past this bound, which is
  why boot2.asm:546 stays as the exact check.
- `KZ_MARGIN_K` mirrors boot2.asm's KZ_MARGIN = 64, and a guard checks the
  two are equal.
- The formula uses no pass-2 KZ number, and BLOB_SEG_AT is assembled into
  `.ovl`'s DSK_HCYLSEG, so it is pass-stable.
- The floor of 96 makes wave 0 byte-identical. Today's bound would give 64.
- NASM's `%if` cannot see forward references, so the `max` is written
  arithmetically (NASM's relational operators evaluate to 0 or 1 in an
  ordinary expression). That is the implementer's detail. BLOB_SEG_AT is
  used as an instruction immediate (DSK_HCYLSEG, `B2CALL`), whose size does
  not depend on its value, so the forward reference creates no size cycle;
  the `%assign` lines at the foot (`KS_BLOB`, `KS_BOOTMX`) come after
  `.boot3`'s size is known.
- boot2.asm:546 stays as the exact pass-2 check.
- Stage 1's MEMFIT and boothd's BLOB_SEG already read `blobseg` off the `ks:`
  line, so neither changes.
- **kern_small's page term (PREDICTED; confirm before acting).** kern_small's
  11-sector blob, at 0xF200..0x10800, straddles the 64KB DMA page 7 sectors
  in (section 12, *Found on the way*), which probably costs stage 1 a third
  call. On kern_small alone the derived lift also rounds the blob up to
  0x10000 when `[blob, blob + BOOT2_PAD)` would cross it: at most
  BOOT2_PAD of boot RAM, against guard 5's 60,928 bytes of headroom at
  128KB. It is a term only if `tools/os88boot.py` on small360 confirms the
  third call. kern_big's and kern_mega's blobs straddle no page in any wave
  (0x18600..0x1A800 and 0x21600..0x23C00 at their largest).

## 4. How code calls it, and how calls are fenced

**Contract.** A `.boot3` body has exactly a `.boot2` body's contract, on every
kernel:

- CS = `B3_SEG`, DS = KERNEL_SEG, SS = LOW_SEG.
- It is far-entered and ends in `retf`.
- Its own data is reached through `cs:`.
- It reaches `.text` through the existing KERNEL_SEG shims (`ovw_*`,
  `spw_near`, `cw_*`).
- It reaches `.cold` by a plain `call COLD_SEG:x`, re-pointed by the fourth
  group and `rt_b3` where `.cold` can move (section 4.1).

| macro | once the kernel's `.boot3` is on | before that (kern_small and kern_mega until their wave 1) |
|---|---|---|
| `B3SECT` | `section .boot3` | `section .boot2`, or for a former `.ovl` body whatever its arm used: `mem_init_x` is `.ovl` on kern_mega and `.boot2` on kern_small (OVBCALL's arms), so its fallback is OVBCALL's choice, not a fixed `.ovl` |
| `B3CALL x` | `call B3_SEG:x` (5 bytes; OVLCALL's shape: a constant segment, nothing published, nothing retired). Inside `.boot3` it is `push cs / call x` | BLOBCALL (4) or OVLCALL (5), whichever matches the body's home |
| `B2CALL x` | from `.boot3` into a blob entry: `call BLOB_SEG_AT:x` (5). A target that kmain_o reaches by BLOBCALL already ends in `retf`. A target the blob only NEAR-calls ends in `ret` and needs a `retf` (a single-caller body) or a 4-byte `call x / retf` door (a body with near callers that stay, such as `bvs_disp_init`). BLOB_SEG_AT is the very constant stage 1 (HEAP_PARA) and boothd (BLOB_SEG) are built with | — |

**Once every kernel is on, the fallback column is deleted**, and with it the
per-kernel arms of `B3SECT` and `B3CALL`. That is the owner's "same boot
ladder": a body marked `B3SECT` is in `.boot3` on all four kernels and is
fenced by the same `os88ovlchk` rules everywhere. What still differs by
kernel is only which bodies a kernel assembles at all.

**`B2CALL` is a new assumption, and it is a trade.** Today nothing in the
kernel assumes where the blob is: `[spl_fseg]` is what stage 2 published,
`cfx_slide` re-points the blob's group through it, and memory.inc's
mem_init_x says why it is asked rather than assumed. A constant `B2CALL`
works only while the blob is at exactly BLOB_SEG_AT, so a loader built from a
different define set (t_vbrseg's SPEC.md 52.10.2.1 incident) would send
`.boot3` into the wrong RAM where today only `[spl_fseg]`'s users would be
affected. The alternative that assumes nothing is the existing `SPLCALL`
(through `[spl_fp]`/`[spl_fseg]`, 19 bytes a site by encoding against 5, and
it banks the flags, so a CF answer needs `SPLCALLC`). The plan takes
the constant and puts the fence in `t_bootfloor`'s HEAP_PARA/BLOB_SEG check
(section 13). Section 15 question 9 asks the owner.

Until a kernel's own wave 1, its arm assembles today's bytes, so kern_small
and kern_mega stay byte-identical through wave 0. This is OVBCALL/OVSCALL's
dual-arm pattern (kernel.asm's OVBCALL), kept only for the transition.

### 4.1 Who re-points `.boot3`'s COLD_SEG words

| who | kernel | when | change |
|---|---|---|---|
| `cfx_slide` | kern_big, kern_emu | kmain_o MARK 10, mono machine at home | after the blob's group: `mov ax, B3_SEG / call cfx_grp`, **+6 transient bytes**. `os88cfx.py` emits a fourth group; `cfx_tab` stays about 195 of 256 |
| `rp_walk`, ROM kind (osrom.asm:553) | kern_big, kern_small | kmain_o's `call far [10]` (rom_patch), its second act, right after `OVLCALL dsk_boot_from_x` and long after `kz_all`, so `.boot3` is aboard | one more `rp_list` over `rt_b3` at `RT_B3SEG`: `mov ax / mov si / mov cx / call rp_list / jc` is 14 ROM bytes (by encoding), 0 RAM. On kern_big the site words move from `rt_blob` to `rt_b3`, so the lists' total is unchanged. kern_small's ROM gains the same 14 bytes in its wave 1, when `font_init`'s one site moves (`romsmall` is the row). No ROM_FMT bump: the lists are the ROM's own, and the ROM accepts only the kernel it was cut from (SPEC.md 2.10.4) |
| `rp_walk`, HMA kind (COLDHMA.SYS) | kern_big | inside `drv_boot_x`, while `.boot3` is live | the same list. Unlike `rt_win`, it is **not** skipped: `.boot3` outlives the mount |
| `os88rom.py` | kern_big, kern_small | build | classifies `.boot3` sites into `rt_b3`. Today it skips `.cfx` whole in its identity compare (os88rom.py:471) because `cfx_tab` is stamped after the assembly; once `.boot3` carries code the ROM re-points, that skip would leave it unchecked, so compare `.boot3` with `cfx_tab` blanked (`os88cfx.blank()` already exists). `.boot3m` (wave 5) is classified as a MODULE image, not as `.boot3` code: it is never run in place, only copied, and the copy is fixed the way every module is (wave 5) |
| — | kern_mega | — | nothing: no ROM or HMA image is cut from it, its `.cold` stays at COLD_RAM until `mega_enter`, after the drop, and COLD_SEG is the same number in both modes |

A site nobody re-points cannot ship. `os88cfx.py` ("a site ... in no
section") and `os88rom.py` ("in no section the ROM can reach") already fail
on an unclassified COLD_SEG word, and `rp_walk`'s look pass refuses an
adoption whose words disagree.

### 4.2 Fences: all at build time, 0 resident bytes

There is nothing to retire at the drop and no runtime guard: the `.ovl`
trade, and the owner's rule. `.boot3` is aboard exactly when the blob is and
stage 2 has finished:

- the floppy's stage 2 reads all KZ_SECS before `jmp KERNEL_SEG:0`;
- boothd reads `[ksecs]`, the whole file, then calls `kz_hd`;
- the canary's `.rerun` re-expands everything.

`kmain_o` is entered only through `SPLGATE1`'s `[spl_fseg]` test, and kmain
zeroes `[spl_fseg]` one instruction before `mem_unblob_x`.

The static half is new `tools/os88ovlchk.py` rules, run on every kernel's
build. Each lands with a break-it-on-purpose fixture (docs/WRITING-TESTS.md).

| rule | refuses | catches |
|---|---|---|
| B1 | any reference into `.boot3` except from `.boot2` outside the load-time cone, from `.boot3` itself, or a `tests/ovlrefs.txt` line that answers *"what runs this after `kz_all` and before `mem_unblob`"* | a resident, `.cold`, `.ovl`, module, ISR or CTRL.DRV/HIBER.DRV expansion of a shared macro calling dead RAM |
| B2 | the **load-time cone**: anything reachable from `boot2_entry`, `read_run`, `kz_all`, `kz_hd`, `kz_expand`, `spl_tick` (with `spl_chrome`, `vid_detect`, `bvs_apply` and the `spw_near` targets), `spl_isr` or `spl_anim` naming `.boot3`. Also, `boot/boot2.asm` and `kernel/splash.inc` may not name it at all | first-tick code running packed bytes, the SPEC.md 2.9.4 class. `bvs_apply` is the trap: it looks like "display, not splash" |
| B3 | a CS store, an IVT write or an emitted far pointer naming `B3_SEG` (rule 2b's shape) that outlives the call it is handed to. Two transient hand-downs are legitimate and need a registered exception, not a refusal: DRV_ATTACH's `mov di, %1_att / mov si, cs` (drv_load_at far-calls the hook through DI:SI and returns), and AS_WALK's `push cs / pop es` buffers, which `cw_toast_show` copies and `dsk_find_x` fills. Wave 5's `[rom_mfp]` swap is a third, restored by the same routine before it returns | something that outlives kmain |
| B4 | a near call or BLOBCALL across `.boot2`/`.boot3`. `.boot3` becomes the fifth address space in the existing near-call check, beside `.text`/`.vga`, `.cold`, `.blob` and `.ovl`. This includes the KGO_PAIR hops (`call bvs_cgo / dw target`), which read their inline word through CS and so must be in the caller's segment | two segments sharing offsets |
| B5 | a `cs:` reference in `.boot3` naming a label outside `.boot3`/`.boot3b`; and an IMMEDIATE in `.boot3` naming a `.boot2` label that is then read through the blob's CS (`drv_boot_x`'s `add si, bdt_base`, handed to `ovl_spl_msg_drv`, whose `spl_mcs` reads `[cs:si]`), unless registered | the `ovc_buf` trap (section 2), and its mirror, a blob offset carried across |
| B6 | `OVLCALL` from a `.boot3` body that runs after `drv_boot_x`'s mount (rule 2e, extended); a `KZ_*`, `b2_*` or post-`boot2_entry` label in `.boot3` | the forfeited window half; the two-pass identity |
| B7 | `section .boot3` in a file `kerndos/kdos.asm` includes (`disk.inc`, `diskw.inc`, `dskwin.inc`, `hbstage.inc`), unless `kerndos/kdlayout.inc` declares it | an undeclared section, placed silently in the DOS core under `-f bin` |
| B8 | `B3CALL`/`B3SECT` outside the arms that define B3, MACHALF-style. It retires with the fallback column once all four kernels are on | a macro arm that `os88ovlchk` cannot evaluate |
| B9 | any call or jump into `.boot3m`, from anywhere: it is copied, never run in place, and resident code reaches its copy only through `mod_fp` slots behind the row's segment test (wave 5) | running the image out of the dying section |

**Runtime half: a knob, not shipped.** Under `B3POISON=1`, `mem_unblob_x`
fills `[floor, blob top)` with a trapping pattern before it compacts, so a
late call faults at a known CS:IP. The new soak row `b3poison` boots it in a
private tree, on kern_big and on kern_small, and drives a menu, a file open,
a Control Panel round and a Display switch on MartyPC. To break it on
purpose, call a `.boot3` body from kmain after `mem_unblob_x` and watch the
row go red.

## 5. Per arm (owner, 2026-11-08: generic)

B3 is defined on every kernel. kern_big and kern_emu switch it on in wave 0
(it replaces `.cfx`); kern_small and kern_mega switch it on in their wave 1,
so wave 0 stays byte-identical on both.

| arm | `.boot3` | where |
|---|---|---|
| kern_big, kern_emu (COLD_SLIDE) | yes, from wave 0 | B3_RAM = VGABUF_RAM, with the slide prefix at the head |
| NO_COLD_SLIDE knob | yes | same address. The head is a KVGABUF_SIZE zero pad, which packs to nothing. It needs the section anyway: from wave 1 on, the knob sector (SPEC.md 2.5.3.3.1) is not enough to hold what moved |
| NO_ROM_COLD knob (the A/B) | yes | B3_RAM = KERN_END. The file gap spans LOW and `.vgabuf` as zeros: they pack to a few bytes, and nothing in LOW is live during the load. BLOB_LIFT is derived there too, so it stops being 0. `buildmatrix` is the row |
| kern_small | **yes, from its wave 1** (section 5.1) | B3_RAM = `.cold`'s end (0xEB7). No slide prefix, no `b3_live` rule; the ROM kind walks `rt_b3` |
| kern_mega | **yes, from its wave 1** (section 5.2) | B3_RAM = KERN_END (0x2100), behind a zero gap. Every body is real mode: `mega_enter` runs after `mem_unblob_x` (kmain's order, kernel.asm:6106-6210) |

### 5.1 kern_small: it does not need the room, and it opts in anyway

The owner's condition was "unless small truly doesn't need it". Read off the
code and `build/smallk/kernel.map`:

**What kern_small does NOT need.** Room. Its `.boot2` is 4,930 of 5,632
(702 free) and its `.ovl` 1,474 of 1,536 (62 free), MEASURED. Its blob wants
are BR-1 (−3..+1 net) and BR-6 (+25..+65), and BR-6 runs in the first tick,
so it could not use `.boot3` anyway. Its 128KB boot is not short of RAM:
guard 5's headroom is 60,928 bytes there (section 2).

**What it gains, which the room question does not measure.**

1. **Resident bytes against the 64KB rule.** Wave 5 takes about **310**
   static resident bytes out of kern_small (section 8, wave 5), the kernel
   where a resident byte is dearest and the budget is met exactly at a
   release. Without a `.boot3` there is nowhere boot-time to carry the
   pointer half from.
2. **One ladder.** `font_init` is shared source, today `.boot2` on
   kern_small and `.boot2` on kern_big, and would become `.boot3` on one and
   `.boot2` on the other: two homes for one body, under two sets of rules
   (B2's cone on one, B1-B6 on the other). That divergence is exactly the
   class of mistake the owner named. Generic, the fallback arm is deleted.
3. **A shorter blob, which also clears its DMA-page split.** kern_small's
   non-splash residue (MEASURED spans): the mouse probe (`mouse_init` 365,
   `mou_idbyte`/`mou_idquiet`/`mou_idjudge` 204, `mou_uart` 42 and the three
   `mou_ov_*` doors 17: 628), `font_init` + `fi_time` + `fi_pit` 236,
   `sched_init` 190, `mem_init_x` 66, `snd_init` 59, then `kmain_o` 175 and
   the `bvw_rowcalc`/`bvw_cgo`/`bvw_kgo` instance 179: **1,533**. Moved, the
   residue is about 3,396 and the blob falls from 11 sectors to **7** with
   about 150 free after BR-1 and BR-6. A 7-sector blob at 0xF200 ends
   exactly at 0x10000, so stage 1's PREDICTED third call (6 + 1 + 4,
   section 12) goes, provided the derived lift keeps the blob off the page
   (section 3.3's page term).

**What it costs (PREDICTED).**

- Boot RAM at 128KB: the lift rises to about 160 paragraphs after waves 1
  and 4 and 192 after wave 5 (B3_END about 0xF329 with the image, + 1,086,
  rounded up to 0xF800), or 320 with the page term (blob at 0x10000). Only
  the page term keeps a 7-sector blob off 0x10000: at 160 or 192 paragraphs
  it straddles the page again, 5 or 4 sectors in. Guard 5's
  headroom goes from 60,928 to about 59,392 at worst.
- Load: about 1,700 bytes into the packed tail, about **3.1** packed sectors
  after the bar, against **8** free before an extra stage-2 int 13h at
  blob 11 and **10** at blob 7 (t_blobruns' `stage2_calls`, walked by this
  revision). 0 extra calls.
- The mouseless 128KB machine: wave 5's claim holds a 512-byte granule for
  the session, about **+202** heap net of the static saving (section 8,
  wave 5). That is the one real price, and it lands on kern_small's own
  machine.

**Recommendation: opt in, with waves 1, 4 and 5.** Waves 2 and 3 have
nothing to move on kern_small: its blob has no SYSTEM.CFG reader or boot
driver loader (`drv_boot_x` is a 1-byte `ret`, MEASURED), and its `.ovl` has
no want named. kern_small's `kernel.bin` is no longer byte-identical from
its wave 1; `small128` (full) and `smallboot`, `romsmall` (soak) are the rows.

### 5.2 kern_mega: the same gains as kern_big, at no conventional cost

- **`.boot2`**: 6,883 of 7,168, 285 free (MEASURED). The same clusters as
  kern_big move, minus `as_boot` (kern_mega's is `as_bootc` in `.cold`) and
  plus `mega_wrprobe` (479) with `mega_gateb` (34) and `mega_gwset` (6),
  which kmain_o reaches by `BLOBCALL mega_wrprobe` (kernel.asm:5941), if
  `os88ovlchk` certifies their closure.
- **`.ovl`**: 5,118, 2 free (MEASURED), and the same WP7/WP8 wants as
  kern_big. Wave 3's 176 bytes (`vid_ctx_init` 63, `sch_idle_start` 35,
  `mem_init_x` 78, all in kern_mega's `.ovl`, MEASURED) give it 178 free.
  **This answers the plan's former question 3 (owner, 2026-11-08): yes.**
- **`BOOT2_RES` stops being a special case.** The first draft kept kern_mega's
  `ovc_buf` past its blob and made BOOT2_RES (driver.inc:1054) and
  `mem_init_x`'s non-small arm B3-conditional. With kern_mega on, `ovc_buf`
  moves into `.boot3b` there too, BOOT2_RES equals BOOT2_PAD on every kernel
  that has it, and the conditional disappears.
- **Costs.** Boot RAM only, on a machine whose floor is 640KB (guard 5's
  mega arm, `MEGA_BOOT_KB`). Load on a 286 is track-bounded (boot2.asm:311),
  which t_blobruns does not model yet (section 13 adds kern_mega rows).
- kern_mega's `kernel.bin` is no longer byte-identical from its wave 1;
  `megaovl`, `megamod`, `megahiber` (soak) and `megahigh` (full) are the rows.

### 5.3 kern_emu

As kern_big, with its 14-sector blob (the knob sector, kernel.asm's
BOOT2_KNOBSEC) and `vmm_boot_x` (about 100, PREDICTED) joining wave 2. There
is no emuk tree here, so its figures are PREDICTED.

## 6. Per load path

| path | how `.boot3` arrives | note |
|---|---|---|
| Floppy at all four geometries | stage 2's packed read, inside its existing runs | priced in section 10 |
| Hard disk, live USB, live CD (El Torito floppy emulation is the floppy path) | boothd reads `[ksecs]` to KERNEL_SEG + R, and the blob's `kz_hd` expands it in place: the same addresses | **an existing hazard, now more frequent**: an installed disk whose VBR (BLOB_SEG, KZ_RPARA, KZ_HD) predates a KERNEL.SYS with a different lift. Every lift step needs a VBR refresh. Rows: `install`, `hdboot`, `hdtakeboot` |
| NOKZIP | the plain flat read; B2_KSECS follows MODC_START | the `.boot3` guard covers the read's 511-byte rounding |
| FLAT_PAYLOAD | none | no stage 2, so `[spl_fseg]` = 0, kmain_o never runs, and nothing calls `.boot3`. Wave 5's row stays at segment 0, so its gates send every key through: that knob build has no keyboard mouse from wave 5 on, which is the safe direction (a slot answering `mod_gone` would eat every key) |
| 5150 ROM (ROM_COLD, ROM_ADOPT), kern_big and kern_small | always off the disk: the ROM holds only `.cold` | `rp_walk` walks `rt_b3` |
| COLDHMA.SYS / HMA_DRV, kern_big | off the disk | the HMA kind walks `rt_b3` inside `drv_boot_x` |
| Canary reload (`.rerun`) | re-read and re-expanded with everything else | KSIG_OFF stays put until wave 4 (section 7) |
| kern_small's 128KB boot | the floppy path | the derived lift costs at most about 3.5KB of guard 5's 60,928 headroom (section 5.1); `small128` (full) boots it. **No kern_big 128KB row (owner, 2026-11-08)**: kern_big boots there and should keep doing so, but nobody runs it that way, and the row that existed was taken out. Its boot arena is a host-side figure instead (section 13) |
| Hibernate resume, kern_big and kern_emu | — | `hb_probe_x` only probes inside kmain_o. The resume runs from `ui_task` long after the drop, and HIBER.DRV's FONT_PICK is its own copy. Wave 5's claim is in the image like any module (wave 5) |
| kern_mega's protected-mode switch | — | after the drop. Wave 5's claim is moved above 1MB at the switch like every module the boot loaded (`mod_hiboot`) |
| Knob loaders (BOOTDIAG, TRACKRUN, CYLRUN) | the same file | the same rules |

## 7. The canary, BLOBSUM, and integrity

- **KSIG_OFF stays put through wave 3.** ~~It is 4,096 on kern_big, inside the
  4,608-byte plain head, at file sector 21.~~ **(build, 2026-11-08)** BR-1's
  fix moved it, with item E: it is **22,528** on kern_big, file sector 57, in
  the packed tail (section 16.6). `.boot3` appends at the far end, and
  BOOT2_SECS does not change before wave 4, so waves 2-3 and 5 leave the file
  sector where it is; `t_canary` re-walks the loader's runs on every build
  and the neighbour-sum test covers the sector's changing contents.
- **The canary moves when BOOT2_SECS falls, on every kernel.** That is wave 4:
  13 → 7 on kern_big, 11 → 7 on kern_small, 14 → 7 or 8 on kern_emu and
  kern_mega. BR-1's fix (1) puts KSIG_OFF at 22,528 and was checked at blob
  lengths 13, 14 and 15 only (synth/bugs.md BR-1), so wave 4 rides boot-10's
  derived KSIG_OFF (synth/bugs.md BR-1, step 4; phase1.md WP6), which emits
  each arm's value. **(build, 2026-11-08)** boot-10 is built (section 16.9):
  `tools/os88ksig.py` chooses it per arm, and walked ahead of wave 4 at blob
  lengths 7 and 8 with heads 5 and 9 it finds a sector on every arm.
- **A body that moves out of the blob leaves stage 1's BLOBSUM**
  (SPEC.md 2.9.7). Like `.text`, it is then checked by nothing but the decode.
  That is the same exposure the rest of the kernel has: a misread tail sector
  runs bad boot code once. This is stated, not fixed. Wave 5's image is
  checked once more, by `mod_need`'s header compare, on the copy.

## 8. What moves, in waves

Spans are MEASURED off each kernel's map (`.boot2`, `.ovl` and `.text`
symbols). Call-site costs are by encoding: BLOBCALL is 4 bytes and a far call
5; a near call is 3. Each wave is one reviewable commit per kernel it
reaches. Per CLAUDE.md's Testing rule, no wave runs a soak row: each names
its rows, and section 13's union runs once after the last wave.

### Wave 0: the mechanism, with nothing moved

- On kern_big and kern_emu, `.cfx` becomes `.boot3`: the section line,
  `MODC_START`, the guards (section 13), and the `b3_live` label (today's
  `cfx_grid` guard). The `.cfx` block moves into `kernel/boot3.inc`
  (section 1).
- Add the macros `B3SECT`, `B3CALL` and `B2CALL` on all four kernels, with
  the fallback arm on kern_small and kern_mega.
- Add the fourth group: `cfx_slide` +6, plus `os88cfx.py`, `rt_b3`,
  `os88rom.py` and `rp_walk` in both kinds.
- Derive the lift (section 3.3) on the kernels that are on.
- **Guard 5 counts the lift, on every kernel.** Fix BR-154 / boot-18: the
  sum at kernel.asm:9703 and `KS_BOOTMX` at :9443 become
  `BLOB_SEG_AT*16 + BOOT2_PAD`. 0 bytes.
- Add the `os88ovlchk` rules B1-B9.
- Add the `kernsize` line: `boot3=` used, free under the blob, the read-end
  margin, and the boot arena at 128KB (section 13). This is WP31's tally,
  with a third home.
- Teach the tools the section: `os88sym` SEGMENTS (`'.boot3': 'B3_RAM'`),
  `os88hot`, `os88ladder`, `os88boot`, `os88modcost`, `tests/coldslide.py`
  and `tests/bootstatus.py`.
- Move `blobruns` to the fast tier and fix its two model gaps (section 13).

Result:

- **kern_small and kern_mega: `kernel.bin` byte-identical.** `cmp` at one
  commit proves it.
- kern_big and kern_emu: `.boot3` = `.cfx` + 6.
- Resident: 0.

**Built (build, 2026-11-08)**, with the deviations section 16.4 lists: `.boot3`
is `.cfx` + **5** (535 bytes), resident 0, BLOB_LIFT 96 (the bound gives 64),
kern_small's and kern_mega's `kernel.bin` and `kernel.sys` byte-identical at
one commit.

### Wave 1: closed clusters, per kernel

**kern_big, kern_emu:**

| body | span | its own data, through `cs:` | COLD_SEG sites | caller |
|---|---:|---|---:|---|
| `hb_probe_x` (hiber.inc) | 76 | none | 5 | `BLOBCALL` in kmain_o, MARK 29 → `B3CALL` (+1) |
| `as_boot` + `as_s_dir` + `as_s_skip` + `as_find` (desksc.inc AS_WALK) | 144 + 9 + 18 + 24 = **195** | `as_s_dir` and the `as_find` answer; they move with it | 5 | `BLOBCALL`, after `spl_finish` → `B3CALL` (+1). kern_mega's copy is `as_bootc` in `.cold` and does not move |
| `font_init` + `fi_time` + `fi_pit` (font.inc) | 200 + 23 + 13 = **236** | none (FONT_PICK's own expansion) | 1 | `BLOBCALL`, MARK 13 → `B3CALL` (+1) |
| **total** | **507** | | **11** | **+3** |

The blob frees **504 net**. `.boot3` gains 507. That puts its end at about
0x181B3, 15 bytes inside the lift's certain bound of 0x181C2
(0x18600 − 64 − 1,022), so the lift stays 96. If the crossings or wave 0's
growth spend those 15 bytes, the derived lift takes +32 paragraphs: 512 bytes
of boot RAM, 0 resident.

**kern_small** (its `.boot3` switches on here, so this is its wave 0 too):
`font_init` + `fi_time` + `fi_pit` (236), the mouse probe cluster (628),
`sched_init` (190), `snd_init` (59) and `mem_init_x` (66, OVBCALL's
kern_small arm): **1,179**, each only if `os88ovlchk` certifies its closure
(`mouse_init`'s doors and `sched_init`'s callers are the ones to check). The
blob goes to about 3,751 used, 1,881 free. `.boot3` about 1,190 with
crossings; the lift goes 96 → about **160** (B3_END about 0xF016, + 1,086,
rounded to 0xF600), PREDICTED. `rt_b3` gains `font_init`'s site. This wave
buys kern_small nothing by itself: it is what lets wave 4 shrink the blob.

**kern_mega** (switches on here): `hb_probe_x` (76) and `font_init` +
`fi_time` + `fi_pit` (238), and `mega_wrprobe` + `mega_gateb` + `mega_gwset`
(519) if their closure certifies: **314..833**. `.boot2` goes to about
6,050..6,569 used, 599..1,118 free. `.boot3` sits at KERN_END; the lift stays
96 at 314 bytes and becomes about 128 at 833 (B3_END 0x21341, + 1,086,
rounded to 0x21800), PREDICTED.

**Built (build, 2026-11-08)**, with the deviations section 16.5 lists, every
kernel's `.boot3` on and the fallback arms deleted. MEASURED: kern_big
`.boot2` 6,478 -> 5,974 (**-504**), `.boot3` 535 -> 1,042, lift 96 (16 bytes
before it steps); kern_small `.boot2` 4,745 -> 3,310 (-1,435), `.boot3`
1,441, lift **160**; kern_mega `.boot2` 6,709 -> 5,713 (-996), `.boot3`
1,010, lift **160**. Resident 0 on every kernel.

### Item E: one tick right after the plain head (recommended)

The owner's condition (owner, 2026-11-08): an option only "if it is less
complex than wave 4". **It is, by a wide margin, and it buys more**, so this
plan recommends it as its own small change, independent of wave 4.

**What it is.** In `boot/boot2.asm`, after the KZIP head loop (`.head_next`)
and before the tail loop, one call: `xor ax, ax / mov dx, KZ_SECS -
KZ_HEADSEC / call spl_tick` (8 bytes by encoding, all in the loader). The
first tick, which draws the splash, probes the adapter and shows the bar at
0, then happens as soon as the head is aboard instead of after the first
tail run. `spl_tick` clamps and stores whatever count it is given
(splash.inc:235-241), so 0 is a legal first count.

**Why it is legal.** The first tick needs exactly the `SPL_RESIDENT` sectors
aboard, and under KZIP `KZ_HEADSEC` *is* `SPL_RESIDENT` (boot2.asm's own
comment at the tail loop). Today the gate is satisfied one tail run late, not
by necessity: the packed tail is not expanded until `kz_all`, so nothing the
first tick may use lives in it. The store rule does not change: a first-tick
store past the head lands where the packed tail will be read, which today
corrupts packed bytes and after item E is merely overwritten; both are
forbidden, and boot-4's audit (phase1.md WP6) is the check either way.

**Code touched and risks, against wave 4.**

| | item E | wave 4 |
|---|---|---|
| code touched | 8 bytes in `boot2.asm`, one site | about 1,200 bytes of bodies on kern_big (KSW_SECT, `vx_boot_x`, `xm_boot_x`, `wm_init`, `ovd_geom`, `kmain_o`), doors, a second KGO pair's users, `dkb_geom`, kmain's gate |
| constants it moves | none | BOOT2_SECS, KSIG_OFF (boot-10), the canary's sector, t_blobruns' IMAGES and STAGE2 rows, the VBR on installed disks |
| preconditions | blob room for 8 bytes: on kern_big wave 1 or boot-7's −41; kern_small (702 free) and kern_mega (285) have it today. Inside the same `%ifndef BOOT_NOSPLASH` as the tail loop's tick | boot-4 and boot-10, waves 1-2 |
| risks | the bar shows at 0 for one tail run longer; a canary `.rerun` re-ticks at 0, which is the bar's existing "climbs from 0 again" behaviour (splash.inc:239-241) | every risk in section 12 that names wave 4, and a stage-2 call after the bar on 360KB/720KB (section 10) |
| BR-1's track-bounded head | independent: the tick goes after BR-1's `pop word [b2_runmax]`, outside the widening block, so `.rerun`'s one-shot guarantee is untouched | interacts: a 7-sector blob makes the 360KB/720KB head cross a track (one call more there) and stops it crossing on 1.2MB/1.44MB (one call fewer) |
| NOKZIP | not applicable: the SPL_RESIDENT gate stays as it is | the same |
| rows | `splashbar`, `splashspin`, `kzboot`, `canary` | section 13's whole wave-4 list |

**Built (build, 2026-11-08)** with BR-1 and boot-7, section 16.6: 8 bytes,
after BR-1's `pop`, inside the same `%ifndef BOOT_NOSPLASH`.

What each buys before the bar is section 10's table. In short (PREDICTED):
item E shows the bar 4 to 18 sectors and one int 13h call sooner on every
disk; wave 4 alone, 18 sectors sooner on 360KB/720KB and nothing on
1.2MB, 1.44MB or small360 (where it removes the DMA-split call instead).
Together they reach 16 sectors and 3 calls before the bar on all five disks.

### Wave 2: the SYSTEM.CFG reader and the boot driver loader, with `ovc_buf` (kern_big, kern_emu, kern_mega)

`drv_boot_x`'s cone, apart from the status line, `xm_boot_x`, the
adapter-switch bodies it reaches (`bvs_switch`, `bvs_disp_init`, which move in
wave 4) and the title table `bdt_*` (all four are explained below the table).
kern_big's spans:

| body | span |
|---|---:|
| `ovc_map`, `ovc_keys`, `ovc_bit`, `ovc_sig`, `ovc_pack`, `ovc_want_set`, `ovc_unpack`, `ovc_key`, `ovc_deser`, `ovc_load` | 624 |
| `ovl_cfg_load` | 4 |
| `ovl_sc_drop`, `ovl_sc_plain`, `ovl_sc_out`, `ovl_sc_free`, `ovl_sc_load` (shell-8's +10 lands here) | 283 |
| `drv_boot_x`, `drv_boot_want` | 266 |
| `ovl_fdd_apply` (near-called from `drv_boot_x`, driver.inc:5814) | 99 |
| `bdl_load`, `bdl_att`, `bdl_hma`, `bdl_tier`, `bdl_publish`, `bdl_call` | 323 |
| `bxf_sync` (EXF_SYNC's blob instance; `drv_boot_x` is its one caller, driver.inc:5867), if `os88ovlchk` certifies its closure | 50 |
| **total moved out of the blob** | **1,649** |
| **added in `.boot3`: `KGO_PAIR b3`**, a second instance of the hop pair (`bvs_cgo` + `bvs_kgo` span 0x170E..0x1746 in the blob, MEASURED) | **56** |

kern_mega's cone is the same list without `bdl_hma` (no HMA kind there) and
with its own `ovc_cfgname` (36) in the blob: about **1,630** (MEASURED spans
from build/megak/kernel.map: `ovc_*` 660, `ovl_sc_*` 283, `drv_boot_x` +
`drv_boot_want` 262, `ovl_fdd_apply` 99, `bdl_*` 272, `bxf_sync` 50,
`ovl_cfg_load` 4).

**What does NOT move, and why (found by the verifier).**

- **`bdt_*` (92) stays in the blob.** `drv_boot_x` does
  `mov si, [bx+DRVR_TITLE] / add si, bdt_base / call ovl_spl_msg_drv`, and the
  status line's `spl_mcs` reads the title as `[cs:si]` in the blob's segment.
  Moved to `.boot3`, the title would be read at the right offset in the
  wrong segment. `drv_boot_x` keeps naming `bdt_base` as an immediate, which
  B5's second half registers.
- **`bvs_cgo` cannot be far-called.** KGO_PAIR's `_cgo` reads its target from
  the `dw` after the call through `[cs:bx-2]` and returns to the caller's CS,
  so it must sit in the caller's segment. `drv_boot_x` calls it four times
  (`call bvs_cgo / dw mod_need`, ...), and DRV_ATTACH's `bdl` instance takes
  it as `%2`, its own hop (`call %2 / dw drv_load_at`). Hence `KGO_PAIR b3`,
  with `DRV_ATTACH bdl, b3_cgo` and the `drv_boot_x` sites spelled `b3_cgo`.
  It is the precedent CTRL.DRV (`cp`) and HIBER.DRV (`hbg`) already follow:
  each image has its own pair.
- **The blob bodies `drv_boot_x` still reaches**, each a crossing:
  `BLOBCALL bvs_switch` becomes `B2CALL` (+1; it is far-bodied already);
  `call bvs_disp_init` is a near call to a body kmain_o also near-calls
  (kernel.asm:5746), so it gets a `call bvs_disp_init / retf` door in the blob
  (+4 in `.boot2`) and `B2CALL` (+2) here; `SPLCALL spl_reset` goes through
  `[spl_fp]` and works from any segment unchanged.

- **`ovc_buf` moves with them**, into `.boot3b`, on every kernel that runs
  this wave. BOOT2_RES then equals BOOT2_PAD everywhere, and BOOT2_RES
  (driver.inc:1054) and `mem_init_x`'s non-small arm reduce to that (section
  5.2). `ovc_buf` is a `dwf_dskw_read` destination (`ovc_load`, ES:BX). The
  disk layer bounces a sector that straddles a 64KB page through `dsk_secbuf`
  (disk.inc's `.drvcut`), so alignment is not a correctness rule here, but
  start `.boot3b` on a linear 512-byte boundary anyway (SPEC.md 1's rule, and
  no bounce).
- The 11 COLD_SEG sites move into group 4 on kern_big: `ovl_sc_load` 4,
  `ovl_sc_free` 1, `ovc_unpack` 2, `ovc_load` 1, `drv_boot_x` 1, `bdl_publish`
  1, `bdl_hma` 1. kern_mega re-points none (section 4.1).
- Crossings, PREDICTED:
  - kmain_o → `drv_boot_x`: +1 in `.boot2`.
  - `drv_boot_x` → `ovl_spl_msg_cfg/drv/boot`, which stay with the status
    line: `B2CALL`, +2 each in `.boot3`. Their `ret` becomes `retf`, 0 bytes;
    kmain_o's other status-line calls already expect `retf`.
  - `drv_boot_x` → `xm_boot_x`: `B2CALL`, +1. `xm_boot_x` stays, because it
    shares `bvs_cgo` with `vx_boot_x`.
  - `ovc_load` → `cfx_cfgname` is now near: −2 (kern_big, kern_emu).
  - kern_emu's `vmm_boot_x` may join (about 100, PREDICTED).
- `os88ovlchk` B1-B6 must certify the closure before the commit. A body it
  rejects stays in `.boot2`.

Result on kern_big (corrected by the verifier; the first draft moved `bdt_*`
and missed the pair and the doors): `.boot2` frees about **1,640** (1,649
moved, less kmain_o's +1 and the `bvs_disp_init` door's +4). `.boot3` grows
about **1,715** (1,649 + the 56-byte pair + about +10 of crossings: three
status-line `B2CALL`s +6, `xm_boot_x` +1, `bvs_switch` +1, `bvs_disp_init` +2,
with `cfx_cfgname` near −2), plus 512 nobits. The derived lift goes 96 →
**224** paragraphs (PREDICTED). It is bound by the read: about 0x181B3 +
1,715 = 0x18866, + 1,086 = 0x18CA4, rounded up to 0x18E00. On kern_mega the
lift goes to about **256** (B3_END about 0x219E2, + 1,086, rounded to
0x22000), PREDICTED.

**Built (build, 2026-11-08)**, with the deviations section 16.7 lists.
MEASURED (the shipped KZIP maps): kern_big `.boot2` 6,127 -> **4,489**
(-1,638), `.boot3` 1,042 -> 2,754 + 926 nobits (`.boot3b`: a 414-byte pad to
the linear 512-byte boundary and `ovc_buf`'s 512), lift 96 -> **224**;
kern_emu 6,244 -> 4,605 (-1,639), `.boot3` 2,755, lift 224; kern_mega 5,866
-> 4,281 (-1,585), `.boot3` 2,670 + 914, lift 160 -> **256**. Resident 0 on
every kernel; kern_small byte-identical. `ovc_buf` moved on kern_mega too,
and `BOOT2_RES` is gone from every kernel (section 16.7, deviation 2).

### Wave 3: relieve `.ovl` (kern_big, kern_emu, kern_mega)

| body | span (kern_big; kern_mega the same, MEASURED) | caller |
|---|---:|---|
| `vid_ctx_init` | 63 | `OVLCALL` → `B3CALL`, 0 bytes |
| `sch_idle_start` | 35 | `OVLCALL` → `B3CALL`, 0 bytes |
| `mem_init_x` (OVBCALL's kern_big arm; its one `wm_cbd` site leaves group 2 for group 4; it runs after the slide, which re-points both) | 78 | OVBCALL, 0 bytes |
| **total** | **176** | |

All three run before the mount. `.ovl` goes from 1 free to **177** on
kern_big and from 2 to **178** on kern_mega (owner, 2026-11-08: kern_mega
opts in; the plan's former question 3). kern_small's `.ovl` has 62 free and
no want named, so it has no wave 3.

**Option**, if BR-157/158 and WP8 want more: the clock ladder from `clk_init`
(one OVLCALL entry, MARK 6). It is **1,782 bytes, MEASURED**: one contiguous
run of `clk_*` symbols in `.ovl`, 0x701 (`clk_init`) to 0xDF7 (`mem_init_x`),
with nothing else between, although clock.inc interleaves `section .text`
blocks in the source. Before moving it whole, check that no rung is reached
from outside `clk_init`'s cone (they are `.ovl` today, so none is reached after
the mount, but B1 asks the same of the drop). Load time: moving `.ovl` bytes
costs about 0 read. `.ovl` is in the same packed stream (file 0xCE00, MEASURED),
and the zeros it leaves behind pack to almost nothing. The lift absorbs about
1.0 byte of R per byte moved. Whole or in part is question 8 (answered: whole).

**Built (build, 2026-11-08)**, with the clock ladder WHOLE and the deviations
section 16.8 lists. MEASURED (the shipped KZIP maps): `.ovl` kern_big and
kern_emu 5,119 -> **3,161** (1 -> **1,959** free), kern_mega 5,118 -> **3,160**
(2 -> **1,960** free); `.boot3` kern_big 2,754 -> 4,746, kern_mega 2,670 ->
4,662; the lift 224 -> **352** (kern_big, kern_emu) and 256 -> **384**
(kern_mega), not "same": the ladder's 1,782 bytes were not in the 176 the
ledger priced. KERNEL.SYS keeps its sectors. Resident 0; kern_small
byte-identical.

### Wave 4: `.boot2` back to its original purpose, on every kernel (owner, 2026-11-08)

The owner's answer: *"why keep something bigger than it needs to be? The
quicker splash is a benefit."* It is part of the plan proper.

**Preconditions.**

- **boot-4** (BR-153, phase1.md WP6): SPL_RESIDENT derived from
  `SPL_RES_SIZE`, after its audit of every first-tick store for an address in
  sectors 5-8. It lowers SPL_RESIDENT, and with it KZ_HEADSEC, from 9 to 5,
  so the head the canary and item E sit on changes shape. **(build,
  2026-11-08) Built, with one deviation (section 16.9):** SPL_RESIDENT is
  derived (5, and 6 on kern_mega) and the audit is a gate, but the plain head
  is `max(SPL_RESIDENT, KZ_HEADMIN)` and KZ_HEADMIN stays 9 on kern_big,
  kern_emu and kern_mega: a 5-sector head costs kern_big's 360KB and 720KB
  disks a stage-2 call (11 -> 12), which nobody had priced. kern_small takes
  5. Wave 4 is where kern_big's head is decided, with its blob: at 7 sectors
  5 or 6 is the best head on every disk.
- **boot-10** (phase1.md WP6): KSIG_OFF computed per build arm by a generator
  `t_canary` does not import, because wave 4 moves every blob length and BR-1's
  sector 57 was checked only at 13-15 (section 7). **(build, 2026-11-08)
  Built (section 16.9).**
- Waves 1 and 2 on that kernel, so that what remains in `.boot2` is the
  residue below.
- `blobruns` in the fast tier with its model gaps fixed (section 13), so the
  STAGE2 ratchet fails `make` if the new layout spends a stage-2 call.

**kern_big and kern_emu.**

1. **The adapter switch's boot instance (KSW_SECT) moves, apart from
   `bvs_apply`.** That is `bvs_avail_test` through `bvs_kgo`: **614,
   MEASURED** (0x14E0..0x1746). Wave 2 has already taken `bxf_sync` (50) and
   given `.boot3` its own pair, so the bodies that move here use `b3_cgo` and
   `b3_kgo`. The blob's `KGO_PAIR bvs` (56) then serves only the bodies that
   stay, and goes if none of them uses it. `bvs_disp_init`'s wave-2 door goes,
   too. `bvs_apply` gets a 4-byte `call bvs_apply / retf` door in the blob.
2. **`vx_boot_x` + `vx_blk` move** (124 + 58). `mov dx, cs` hands VESA.DRV
   `B3_SEG:vx_blk`, so first verify that VXV_INIT copies the block and keeps
   no DX:SI.
3. **`xm_boot_x` (102), `wm_init` (34) and `ovl_dock_geom` + `ovd_geom` (75)
   move.**
   - `dkb_geom`'s resident `SPLGATE1` becomes a `[spl_fseg]`-tested
     `call B3_SEG:`, **−4 resident**. Its one B1 exception is registered.
4. **`kmain_o` itself moves** (232). kmain's `SPLGATE1 kmain_o` (9 bytes of
   `.text`) becomes `call B3_SEG:kmain_o` (5), **−4 resident**.
   - That drops the `[spl_fseg]` test `spl_gate` carried (kernel.asm's
     `spl_gate`). It guarded a kernel entered without stage 2, which could not
     run anyway with kmain_o skipped. Say so in SPEC.md 2.9.5.3, and register
     kmain's site as B1's second exception beside `dkb_geom`'s.
   - `dkb_geom`'s −4 below is right as written: it already tests
     `[spl_fseg]` itself (dock.inc:215) before its SPLGATE1.
   - Its calls into the other moved bodies become `push cs / call`.
   - Its 8 calls to the bodies that stay become `B2CALL` (+1 each).
   - boot-17's kmain_o table can ride along for its diagnostic value alone.
5. **What stays in `.boot2` is then MEASURED at 3,215 bytes:**
   - the loader, 605;
   - the splash, 1,788;
   - the first-tick probe, 407 (`vid_detect`, `vid_init`, `bvs_apply`,
     `bvs_vtab`);
   - the status line, 415.

   With `bdt_*` (92) and the doors, about **3,330**. BOOT2_SECS falls from 13
   to **7** (kern_emu from 14 to 7 or 8), with about 254 free for BR-1's head bound (+12),
   BR-6 (+60..+110), item E (+8) and BR-147 (+7).
6. **Then `.boot2` is a ratchet.** A new host gate keeps the list of
   non-splash `.boot2` bodies in `tests/blobbodies.txt`, and that list only
   shrinks. That is the owner's "original purpose", enforced by the build.
7. **The blob is one stage-1 call only at 4 sectors or fewer** on
   os8088-360 and -720, where KERNEL.SYS starts at LBA 14 (C0 H1 S6);
   `t_blobruns.py --sectors 4`, run by the verifier. A 7-sector blob is still
   two calls there (4 + 3, walked by this revision). The "6 or fewer" figure
   is small360's, where KERNEL.SYS starts at LBA 12.

**Built (build, 2026-11-08)**, with the deviations section 16.10 lists.
MEASURED (the shipped KZIP maps): `.boot2` kern_big 4,456 -> **3,296**,
kern_emu 4,572 -> 3,311, kern_mega 4,248 -> 3,213, kern_small 3,463 -> 3,102;
`BOOT2_SECS` **7 on every kernel**; resident **-22** bytes of `.text` on
kern_big, kern_emu and kern_mega and -18 on kern_small (no rung crossed);
the plain head is the window on every kernel; KERNEL.SYS 5 to 6 sectors
shorter; stage 1 two calls on every shipped disk.

**kern_small.** `kmain_o` (175) and the `bvw_rowcalc` + `bvw_cgo` + `bvw_kgo`
instance (179; a KGO pair, so it moves with every caller or stays whole):
about **355**. Residue about 3,396, BOOT2_SECS 11 → **7** (section 5.1),
with the page term keeping the blob off 0x10000.

**kern_mega.** `vx_boot_x` + `vx_blk` (199), `wm_init` (34), `ovl_dock_geom`
+ `ovd_geom` (75), KSW_SECT less `bxf_sync` (699, MEASURED from
build/megak/kernel.map) and `kmain_o` (189): about **1,196**. Residue about
3,240 with `mega_wrprobe` moved in wave 1, BOOT2_SECS 14 → **7**; about
3,760 and **8** if it stayed.

**The earlier bar it buys, quantified (PREDICTED; section 10 has the
table).** On the 8088's cylinder-bounded read, the first tick waits for the
end of the cylinder the head ends in. Wave 4 alone therefore shows the bar
**18 sectors sooner on 360KB and 720KB (about 0.4 s)**, because today's head
ends exactly on a cylinder boundary there and the first tail run is a whole
cylinder; on **1.2MB, 1.44MB and small360 it shows it no sooner**, because
the first tail run grows by what the blob lost. On small360 it removes the
DMA-split call instead (about 0.2 s, if the page term lands). With item E in
place, wave 4 adds the sectors the blob lost: 6 on kern_big's disks, about
**0.13 s** on 360KB/720KB and **0.07 s** on 1.2MB/1.44MB, and 4 on small360,
about 0.09 s.

### Wave 5: the keyboard mouse's pointer half (owner, 2026-11-08)

The owner's shape, preferred over a `KBMOUSE.DRV` module: the pointer half of
the keyboard mouse rides `.boot3`; before the section is dropped, when no
mouse has been found, it is copied into a heap claim; the claim is dropped
when a mouse first speaks. **No disk read, no file on any disk, no
system-disk dependency**, and so none of rt-2's boot cache
(followup-decisions-6-8.md item 6's first dependency is gone). It needs only
wave 0's section on that kernel and is independent of waves 2-4, with one
transient cost if it lands before wave 2: `.boot3` reaches `mod_need`, a near
`.cold` body, only through a KGO hop, and `KGO_PAIR b3` arrives in wave 2.
Before that, and on kern_small, which has no wave 2, wave 5 brings the pair
itself (56 transient bytes, 0 resident; found by the second verifier).

**What moves (MEASURED spans, identical on all three maps).** Read off
kernel/mouse.inc:2451-3632 and build/kernel.map, build/smallk/kernel.map,
build/megak/kernel.map:

| moves into `.boot3m` | span | why it can |
|---|---:|---|
| `kbm_tab` (the eight directions) | 24 | read-only; read through `cs:` in the image (+1 byte at each of five sites, in the image: `.find`'s `[bx]`, and `kbm_move`'s `[bx+2]` twice, `[bx]` and `[bx+1]`). `menu_kbnav`, called with BX on the row, reads no table byte |
| `kbm_key`'s body | 60 | entered only from `ui_task` (ui.inc:214) and `kbm_poll0`, task context |
| `kbm_btn` | 43 | from `kbm_key` and `kbm_p5spend` only |
| `kbm_rclick` | 20 | from `kbm_key` only |
| `kbm_move` | 147 | from `kbm_key` only |
| `kbm_p5spend` | 16 | from `kbm_poll0` and `kbm_ui`'s tail only |
| `kbm_ui` | 69 | from ui.inc:722, behind `[mou_ptr]` |
| `kbm_post` | 9 | from `kbm_btn`, `kbm_rclick` and `kbm_ui` only |
| **total** | **388** | |

**What stays resident, and why.**

- `kbm_isr` and `KBM_SCAN`: IRQ1's handler for every machine. It wakes the
  UI task on a key and guards the BIOS buffer (mouse.inc:2893-2950).
- `kbm_shf` (10): read by `kbm_isr`'s keypad-5 test as well.
- `kbm_pollm`/`kbm_poll`/`kbm_poll0` (39): `menu_track`, `ui_drag`,
  `ui_grow` and `osapi_mouse` call them on every machine, because they eat the
  keys a modal loop would leave wedged in the BIOS buffer and tail-jump to
  `kbd_ovspend` (followup-decisions-6-8.md's correction to WP3).
- `kbm_pass` (2), the `kbm_key` stub's refusal.
- `mou_clamp` and `mou_evfill`: shared with the serial and PS/2 paths.
- **The state, 15 bytes plus `kbm_p5`, `kbm_eat`, `kbm_old9`, `kbm_ivec`
  and `kbd_ae`**. `kbm_ev` (8) must be DS-addressed, because `kbm_post`
  hands it to `evq_push` as DS:SI. `kbm_step`, `kbm_lastk`, `kbm_last`,
  `kbm_btick` and `kbm_slast` (7) are written by the moved code, and on
  kern_mega the image runs as a module's CODE half above 1MB, which cannot be
  written. Moving them would also cost a segment load per access, for 7 bytes.
  So "their data" here is the table; the state stays in DS on all four
  kernels, the module convention (kernel/mod.inc: "DATA stays in the kernel
  segment").

**The mechanism: a module row adopted from memory.** It reuses kernel/mod.inc
whole: its row, its slots, its thunks' gate, its header check, its fixups and
its drop.

1. **The image.** `.boot3m`, a section of its own at vstart=0 after `.boot3`
   in the file (before MODC_START, so it is in KERNEL.SYS and in
   `kernel.bin`, and `os88mod.py` does not cut it). It begins with a module
   header (MOD_H_*: 12 bytes, plus 2 per entry, 18 with three entries) and
   holds the 388 bytes above with their crossings: about **443** bytes, an
   upper estimate (header 18, far calls out at +2 each, 20, the five `cs:`
   prefixes, entry exits 6, and a `[mou_ptr]` test in `kbm_ui`, 7).
   An assembly-time guard keeps it at or under 512.
   - **The exits, checked by the second verifier.** The three entries are
     far-entered, so every exit `kbm_key`'s body can take (`kbm_pass`,
     `kbm_btn`, `kbm_rclick`, `kbm_move`) and `kbm_p5spend`'s become `retf`
     for 0 bytes; `kbm_ui`'s tail `jmp kbm_p5spend` then still works. The one
     body with two contracts is `kbm_btn`: `kbm_key` tail-jumps to it and
     `kbm_p5spend` near-calls it, so that call becomes `push cs / call
     kbm_btn` (+1). No door is needed, so the 6 above is slack.
   - The image's labels need names of their own (the flat namespace):
     the resident stub keeps the name `kbm_key`, so ui.inc:214 and
     `kbm_poll0` are not edited.
2. **The row.** `MOD_KBM`, after every existing id (MOD_SETS's precedent: a
   row that is not an image of its own goes last, so no image's id moves),
   with a `MODFP KBFP, MOD_KBM, 3` block. Its file name is one no disk
   carries, `'KBMOUSE',0`, which the RAM view shows as the claim's name
   (`osapi_img_name`).
3. **The adoption, in `kernel/boot3.inc` (0 resident bytes).** At kmain_o's
   end, after every boot-time probe (`mouse_init`, the PS/2 reset, `drv_boot_x`
   and its USBMOUSE.DRV, kern_emu's `vmm_boot_x`) and before kmain zeroes
   `[spl_fseg]`: if `[mou_ptr]` is 0, install a door in `[rom_mfp]` for one
   call, `mov al, MOD_KBM / call b3_cgo / dw mod_need`, and put the saved
   door back. `mod_need` is a near `.cold` body (it leaves through
   `kretc_es`), so `.boot3` reaches it through its own KGO hop, the way the
   blob's `drv_boot_x` does today (`call bvs_cgo / dw mod_need`,
   driver.inc:5829).
   The door answers ROMF_SIZE and ROMF_COPY for MOD_KBM's row out of
   `.boot3m` (the same "a door that has the bytes" `mod_need` already asks
   first, SPEC.md 2.10.5) and forwards every other verb, ROMF_FIX included,
   to the door it replaced, so a ROM machine's ROM still re-points.
   `mod_need` then claims top-down (`mem_claimp_hi_x`, MEM_K_MOD, never
   moves), copies, compares the header, arms the three slots and runs the
   kernel's own fixup: `mod_cfix` on kern_big and kern_emu (COLD_SLIDE), the
   ROM's ROMF_FIX on kern_small, `mod_hi` at the switch on kern_mega.
   - The predicate is `[mou_ptr]`, "has a pointer SPOKEN", not `[mou_idany]`:
     a mouse that answered identify but never sends a packet still needs the
     keyboard mouse (SPEC.md 9.6.6). The price is that a machine whose serial
     mouse has not been touched by the end of the boot holds the claim until
     its first packet, a few seconds.
   - kern_mega has no ROM_ADOPT and so no `[rom_mfp]` and no `rom_none`
     (kernel.asm:4693-4711 are under `%ifdef ROM_ADOPT`). It gets the door
     word for this one use: **+4 bytes of conventional `.text`**, initialised
     data and not `.bss`, because `mod_need` far-calls it on every load for
     the rest of the session and a zero seed is a call to 0000:0000. Its seed
     is `COLD_SEG:mod_gone`, which refuses SIZE and COPY with every register
     kept, as `rom_none` does, and is valid in protected mode, COLD_SEG being
     selector-compatible there (KERNEL_SEG, `rom_none`'s seed segment, is
     not). And **about +16 of `.cold`**, by encoding: the ROMF_SIZE and
     ROMF_COPY arms that `mod_need` assembles only under ROM_ADOPT. That
     `.cold` runs above 1MB after the switch. (Corrected by the second
     verifier; the first figure was +4 of `.bss` and +3 of `.cold`.)
4. **The gates, resident.** A slot that is not armed answers `mod_gone`,
   CF = 1, and for `kbm_key` CF = 1 means "spent": it would eat every key. So
   every resident entry tests the ROW, not `[mou_ptr]`:
   - `kbm_key` becomes a 12-byte stub: `cmp word [row], 0 / je kbm_pass /
     call far [KBFP] / ret`. The image's own `kbm_key` keeps the
     `[mou_ptr]` test, for the passes between a settle and the drop.
   - `kbm_poll0`'s `call kbm_p5spend` becomes the same gate inline (+8).
   - ui.inc:720's site tests the row and far-calls slot 1 (+1).
5. **The drop.** `mou_hotplug`'s `.have` arm (mouse.inc:1380), which runs
   once, on the UI task, on the first pass after `[mou_seen]` is set by any
   backend (`mou_settle` for serial, PS/2 and the OSAPI feed USBMOUSE.DRV and
   VMMOUSE.DRV use; kern_emu's `vmm_boot_x` sets it directly,
   vmmouse.inc:338): **11 resident bytes**. `mou_hotplug` is `.text`, which
   has no KGO hop (KGO_PAIR is expanded only in CTRL.DRV, HIBER.DRV and the
   blob), and `mod_drop` is a near `.cold` body, so the site is `call
   COLD_SEG:kbm_dropf` (5, and one more COLD_SEG word for the slide, the ROM
   and the HMA image to re-point) into a 6-byte `.cold` door, `mov al,
   MOD_KBM / call mod_drop / retf`. (Corrected by the second verifier: the
   first figure, 7 "through the KGO hop", assumed a hop `.text` does not
   have.) A drop of an empty row is a no-op (mod_free_row's first compare),
   so a machine that never adopted pays one call, once.
   - **Every setter of `[mou_ptr]`, found by the second verifier**, and each
     sets `[mou_seen]` in the same store run, so `.have` follows each one:
     `MOU_SETTLE_STORES` (mouse.inc:1170), reached through `mou_settle`
     (:1184) from the serial byte path (:3939), the PS/2 packet path
     (:2331) and the OSAPI feed USBMOUSE.DRV and VMMOUSE.DRV use (:4046),
     and inline in kern_small's serial path (:3941); and `vmm_boot_x`
     (vmmouse.inc:338-339), in the blob, which never adopts. Nothing clears
     it, and hibernate brings it back with the image (hiber.inc touches
     neither byte).
   - All but `vmm_boot_x` run in interrupt or worker context, where nothing
     may free a claim, so the drop cannot sit at the stores themselves.

**Price, per kernel (ESTIMATED: nothing is built).**

| | kern_big | kern_emu | kern_small | kern_mega |
|---|---|---|---|---|
| static resident out | 388 | 388 | 388 | 388 (364 if the code half cannot be read, and `kbm_tab` stays in DS) |
| resident glue in: the `kbm_key` stub 12, `kbm_poll0` +8, ui.inc +1, five `retf` shims for the image's calls out (`kbm_shf`, `cur_move`, `sch_wake_ui`, `mou_evfill`, `wm_front`; `mou_clamp`, `gfx_lock`, `gfx_unlock`, `wm_hit` already have doors) 20, the row 4, the name 8, three slots 12 (`.bss`), the `mod_fpt` word 2, the drop 11 (its `.text` site 5 and `.cold` door 6) | 78 | 78 | 78 | 82 (the door word; and about +16 of `.cold` above 1MB, not conventional) |
| **net static resident** | **−310** (KERN_BUDGET, and every adapter class, the code being `.text`) | **−310** | **−310** against the 64KB rule | **−306** conventional (MEGA_RESIDENT), −282 with the table in DS |
| a machine with a mouse that speaks during the boot | −310 for the session; nothing is ever claimed | the same; with the VMware pointer (vmm_boot_x) it is never adopted | −310 | −306 |
| a machine whose mouse first speaks after the boot (USBMOUSE.DRV, a late serial mouse) | 512 held until `.have`, then −310 | the same | the same | the same, in extended memory after the switch |
| **a mouseless machine** | **+202** net for the session: one 512-byte granule (MEM_GRAN 32 paragraphs, memory.inc:468-469) against 310 out | +202 | **+202 on the 128KB machine** | **0 conventional** after `mega_enter` (512 held above 1MB as an `mx_tab` block); 512 conventional only between the adoption and the switch. If `mod_hi` refuses (no XMEM block), it stays low as an LDT selector pair and the cost is +206 |
| packed sectors after the bar | about +0.25: the 388 bytes move within the same packed stream (`.text` to `.boot3m`), so KERNEL.SYS grows only by the header, the growth and the glue, about +133 unpacked | the same | the same, of its 8-10 free | the same |
| boot time | 0 before the bar; about 1 ms more decode and, on a mouseless machine, about 1-2 ms of copy at 4.77 MHz | | | |

The granule is the one structural cost, and it is the followup's correction
again: any image up to 512 bytes holds 512 for the session, and one past 512
holds two (about +700 net). The `%error` at 512 makes that growth a decision,
not an accident.

**Hibernate resume.** kern_big and kern_emu write conventional memory whole,
`0000:0000` to `[mem_top]` (SPEC.md 87.3), so the claim, its row in
`mod_tab` and its slots in `.bss` are all in the image, and a resume puts them
back together. The pointer's BUILD_NUM, MOD_STAMP and `[api_coldseg]` fields
guarantee the same kernel with `.cold` in the same place, so the image's
re-pointed COLD_SEG words are right on wake. A mouseless session resumed on a
machine that now has a mouse is found by `mou_hotplug` and dropped at `.have`,
as at any boot. On kern_mega the claim is above 1MB, an `mx_tab` block, which
the extended section carries like every module (SPEC.md 87.9). kern_small has
no hibernate.

**kern_mega, an LDT selector claim.** The adoption runs in real mode, so the
claim is a segment and `mod_need` arms the slots with it. At the switch,
`mega_mods_fix` gives the slots the conventional code alias and `mod_hiboot`
moves the image above 1MB like every module the boot loaded, which needs
`MOD_KBM`'s bit in `MOD_HIOK`. The image's far calls name KERNEL_SEG and
COLD_SEG, which are selector-compatible constants on kern_mega, so nothing in
it needs converting. `mod_free_row`'s kern_mega arm already frees an
above-1MB pair. What must be checked before the commit: that the code half
`mega_modhif` builds can be READ, since `kbm_tab` is read through `cs:`; if
not, the table stays in DS on kern_mega (+24 there).

**Risks particular to it** are R15-R18 (section 12). The rows are section 13's
wave-5 list, with one new row.

## 9. The ledger, per kernel

Today's figures are MEASURED. After-wave figures are PREDICTED. "Free" is
inside the fixed rung. Guard 5's headroom is at 128KB:
128 × 1,024 − 2,560 − BLOB_SEG_AT × 16 − BOOT2_PAD.

**kern_big** (kern_emu the same, with its 14-sector blob; there is no emuk
tree here, so its figures are PREDICTED):

| | `.boot2` used / free | `.ovl` free (of 5,120) | `.boot3` (+ nobits) | BLOB_LIFT (para) | guard 5 headroom | resident | KERNEL.SYS |
|---|---|---:|---|---:|---:|---:|---|
| today | 6,652 / **4** of 6,656 | **1** | 530 (`.cfx`) | 96 | 22,016 | — | 85,168 (167 sectors) |
| wave 0 | 6,652 / 4 | 1 | 536 | 96 | 22,016 | 0 | ~same |
| wave 1 | ~6,148 / **~508** | 1 | ~1,043 | 96 | 22,016 | 0 | ~+450 (+1 sector) |
| item E | ~6,156 / ~500 | 1 | ~1,043 | 96 | 22,016 | 0 | ~same |
| wave 2 | ~4,508 / **~2,148** | 1 | ~2,758 + 512 | ~224 | ~19,968 | 0 | ~+1,950 (+4 sectors) |
| wave 3 | same | **177** | ~2,934 + 512 | ~224 | ~19,968 | 0 | ~+1,950 |
| wave 4 | ~3,330 / **~254** of 3,584 (7 sectors) | 177 | ~4,170 + 512 | ~320 | ~21,504 (the blob is 3,072 shorter) | **−8** | about net 0 against today (~167 sectors) |
| wave 5 | same | 177 | ~4,170 + 512, and `.boot3m` ~443 | ~320 | ~21,504 | **−318** | ~+130 bytes (0..1 sector) |

The wave 2 and 3 rows are the verifier's correction (section 8 wave 2):
`bdt_*` stays in the blob, `bxf_sync` moves, and `.boot3` gains its own
56-byte KGO_PAIR. The wave 4 row is re-derived by this revision: the residue
is 3,215 MEASURED plus `bdt_*` and the doors; the lift is bound by the read
(B3_END about 0x18DEA, + 1,086 = 0x19228, rounded to 0x19400); and the 6
blob sectors given back pay for the lift's extra 96 paragraphs. Wave 5's
image fits under the same rounding (B3_END about 0x18FA5, + 1,086 = 0x193E3),
so the lift does not move.

- **The boot-time arena at 128KB, as a host figure, not a row (owner,
  2026-11-08).** From the blob's nobits top to the top of RAM (the boot sector
  and its stack are dead by kmain_o): 24,064 today (0x1A200); about 22,528
  after wave 2 (the lift +2,048, `ovc_buf`'s move −512); about 24,064 again
  after waves 4-5 (0x19400 + 3,584 = 0x1A200). `kernsize` prints it (section
  13). There is no kern_big 128KB boot row and none is proposed.
- **Disk:** the freed blob bytes are zero padding inside the fixed rung until
  wave 4 shortens it. The cost is 0..1 cluster on a system disk per wave.

**kern_small** (BOOT2_SECS 11; `.ovl` window 1,536):

| | `.boot2` used / free | `.ovl` free | `.boot3` | BLOB_LIFT (para) | guard 5 headroom | resident | KERNEL.SYS |
|---|---|---:|---|---:|---:|---:|---|
| today | 4,930 / **702** of 5,632 | 62 | — | 96 | 60,928 | — | 54,224 (106 sectors) |
| wave 0 | same, byte-identical | 62 | — | 96 | 60,928 | 0 | same |
| wave 1 | ~3,751 / ~1,881 | 62 | ~1,190 | ~160 | ~59,904 | 0 | ~+1,100 (+2 sectors) |
| item E | +8 | 62 | | | | 0 | same |
| wave 4 | ~3,400 / ~150 of 3,584 (7 sectors), after BR-1 and BR-6 | 62 | ~1,545 | ~160, or 320 with the page term | ~61,952, or ~59,392 with the page term | 0 | ~−600 against today (~−1 sector) |
| wave 5 | same | 62 | ~1,545, and `.boot3m` ~443 | ~192 / 320 | ~61,440 / ~59,392 | **−310** | ~+130 (0..1 sector) |

Its boot arena at 128KB: from the blob plus BOOT2_PAD (0x10800 today) to
128KB, 63,488; about 61,952 (page term) to 64,512 after wave 4, and 64,000
without the page term after wave 5 (host figures).

**kern_mega** (BOOT2_SECS 14; boots on 286+, floor 640KB):

| | `.boot2` used / free (of 7,168) | `.ovl` free | `.boot3` | BLOB_LIFT (para) | resident | KERNEL.SYS |
|---|---|---:|---|---:|---:|---|
| today | 6,883 / **285** | **2** | — | 96 | — | 115,872 (227 sectors) |
| wave 0 | same, byte-identical | 2 | — | 96 | 0 | same |
| wave 1 | ~6,569..~6,050 / ~599..~1,118 | 2 | ~314..~833 | 96..~128 | 0 | ~+300..+800 (+1..2 sectors) |
| wave 2 | ~4,430 / ~2,738 (from 6,050) | 2 | ~2,530 + 512 | ~256 | 0 | ~+1,900 |
| wave 3 | same | **178** | ~2,706 + 512 | ~256 | 0 | same |
| wave 4 | ~3,240 / ~344 of 3,584 (7 sectors), or ~3,760 of 4,096 (8) if `mega_wrprobe` stayed | 178 | ~3,900 + 512 | ~320 | 0 conventional | about net 0 |
| wave 5 | same | 178 | + `.boot3m` ~443 | ~320 | **−306** conventional (MEGA_RESIDENT) | ~+130 |

Guard 5's mega arm (MEGA_BOOT_KB) has room for all of it; its figure is not
re-derived here.

## 10. Load time (PREDICTED)

### 10.1 Before the bar

**Waves 0-3 and 5 change nothing before the bar.** Stage 1 still reads the
fixed rung, the first tick still follows the plain head, and `.boot3` is the
end of the packed tail, which the bar already measures (its total is KZ_SECS −
KZ_HEADSEC, so it counts the new sectors itself).

**Item E and wave 4 do.** On an 8088 stage 2 bounds each run at the cylinder,
so the first tick today waits for the end of the cylinder the head ends in.
Sectors and int 13h calls before the first tick, walked with t_blobruns' own
`runs()` and `stage2_calls()` logic over the shipped images (PREDICTED until
`tools/os88boot.py` confirms; kern_small's stage-1 DMA split is not in the
model and would add a call to its "today" and item-E cells):

| disk | today (blob 13; small 11) | item E | wave 4 (blob 7) | both | with BR-1's track-bounded head: today / E / wave 4 / both |
|---|---|---|---|---|---|
| os8088-360 | 40 / 4 | 22 / 3 | 22 / 4 | **16 / 3** | 40/4, 22/3, 22/5, 16/4 |
| os8088-720 | 40 / 4 | 22 / 3 | 22 / 4 | **16 / 3** | 40/4, 22/3, 22/5, 16/4 |
| os8088-120 | 31 / 4 | 22 / 3 | 31 / 4 | **16 / 3** | 31/5, 22/4, 31/4, 16/3 |
| os8088 (1.44MB) | 39 / 4 | 22 / 3 | 39 / 4 | **16 / 3** | 39/5, 22/4, 39/4, 16/3 |
| small360 | 24 / 4 | 20 / 3 | 24 / 4 | **16 / 3** | 24/5, 20/4, 24/5, 16/4 |

Converted at 22.2 ms a sector inside a run on 360KB/720KB and 11.1 ms on
1.2MB/1.44MB, and about one revolution (167-200 ms) for each call saved
(PERFORMANCE.md prices a call at 1-2 revolutions whatever it moves):

| | 360KB / 720KB | 1.2MB | 1.44MB | small360 |
|---|---:|---:|---:|---:|
| item E alone | ~0.6 s | ~0.27 s | ~0.39 s | ~0.29 s |
| wave 4 alone | ~0.4 s | ~0 | ~0 | ~0, or ~0.2 s with the page term |
| both | ~0.73 s | ~0.33 s | ~0.46 s | ~0.4-0.6 s |
| what wave 4 adds once item E is in | ~0.13 s (6 sectors) | ~0.07 s (6) | ~0.07 s (6) | ~0.09 s (4), + the page call |

So the recommendation is **both**, item E first: it is the cheaper change and
the larger share of the benefit, and wave 4 is taken anyway for the owner's
reason, a `.boot2` no bigger than it needs to be. boot-4's 5-sector head
moves the first tick 4 sectors earlier again under item E (the head is one
call at either blob length).

BR-1's track-bounded head interacts with wave 4 and not with item E: at
blob 7 the 360KB/720KB head no longer starts on a track boundary and crosses
one (one call more), while on 1.2MB/1.44MB it stops crossing (one call fewer).

### 10.2 After the bar

**Cost per 512 unpacked bytes moved into `.boot3`.** Each one costs 0.89 to
0.95 packed sectors: 0.886 is the risk design's measurement for blob code
packed after the kernel's history, and 0.95 is block 2's own ratio
(29,096 / 30,642, MEASURED). On top of the read it costs 4.3 ms of LZ4
decode (39.9 cycles a byte, SPEC.md 2.9.13, at 4.77 MHz).

| geometry | ms per sector inside a run | ms per 512 bytes moved | wave 1 (~507) | wave 2 (~1,700) | extra packed sectors before one more stage-2 int 13h, at blob 13 / 7 (kern_small 11 / 7) |
|---|---:|---:|---:|---:|---:|
| 360KB (300 rpm, 9 spt) | 22.2 | ~24 | ~+24 | ~+80 | 17 / **5** |
| 720KB (300 rpm, 9 spt) | 22.2 | ~24 | ~+24 | ~+80 | 17 / **5** |
| 1.2MB (360 rpm, 15 spt) | 11.1 | ~14 | ~+14 | ~+47 | 14 / 20 |
| 1.44MB (300 rpm, 18 spt) | 11.1 | ~14 | ~+14 | ~+47 | 16 / 22 |
| small360, small720 | 22.2 | ~24 | | | 8 / 10 |

- **The last column** is t_blobruns' `stage2_calls` walked over the shipped
  images from today's file: the three designers' walk at blob 13, and this
  revision's at the wave-4 lengths. On 360KB and 720KB the last run is a
  single sector at LBA 180 today; at blob 7 the read ends inside cylinder 9
  and the cylinder boundary is 5 sectors away.
- **kern_big's tail grows by about 6.3 packed sectors over all the waves**:
  about 4 for waves 0-3, about 2.1 for wave 4's ~1,200 bytes, about 0.25 for
  wave 5. At blob 13 that is well inside 17. **At blob 7 it is on the edge
  on 360KB and 720KB**: about 6.3 against 5, so wave 4 is likely to cost one
  stage-2 call AFTER the bar there (about 0.2-0.4 s), unless the tail is
  about a sector shorter. 1.2MB, 1.44MB and both small disks have room.
- **boot-4 moves these thresholds too.** With a 5-sector head and its −1
  sector (phase1.md WP6's cost lens), the walk gives 0 free at blob 13 and 6
  at blob 7 on 360KB/720KB (15/21 on 1.2MB, 17/23 on 1.44MB, 9/11 on
  small360). So boot-4 with waves 1-2 alone already costs that call on the
  two 9-sector disks; wave 4 does not make it worse. Both cases sit within a
  sector of a boundary, which is why the ratchet, not this table, decides.
- **`blobruns` is now the gate that refuses it (owner, 2026-11-08).** It is
  a host check that boots nothing, timed by this revision at **0.032, 0.037
  and 0.052 s wall** (three runs, MEASURED), so it goes to the **fast**
  tier, in the *shipped artifacts* family beside `image` and `canary`
  (docs/WRITING-TESTS.md 2.1): it reads the six system disks' BPBs and root
  directories. A wave that spends a stage-2 call then fails `make`, and the
  STAGE2 row moves only with a reason beside it. Two model gaps are fixed
  with the move (section 13).
- **The room is shared.** Any later kernel growth spends the same sectors.
- **On a 286 the read is track-bounded** (boot2.asm:311), which this model
  does not cover. kern_mega gets rows (section 13).
- **For contrast**, the refused 14th blob sector costs +1 sector *before* the
  bar on every geometry, and on 720KB a whole stage-1 call (4 + 9 + 1).

## 11. What it unblocks in the big review

| item (docs/reports/big-review-2026-11-08/synth/) | where it lands |
|---|---|
| **BR-6**, MDA vs Hercules (bugs.md), alias-test form +60..+110 | **stays in `.boot2`** on every kernel: it runs in `vid_detect`'s mono arm in the first tick, before `.boot3` exists. `vid_memchk` is written once as a macro and expanded in `.boot2` and `.ovl`. On kern_big it is paid from wave 1's ~508 free (~549 after boot-7); kern_small (702) and kern_mega (285) have room today. The 3BAh form is no longer forced by room |
| **BR-1**, the canary (bugs.md), +19..+23 | loader: `.boot2`. Fits after wave 1. Its canary sector moves with wave 4's blob lengths (section 7) |
| **BR-147** (bugs.md), +7 per VID_APPLY expansion | `bvs_apply` is the first tick's copy: `.boot2`. Fits |
| **shell-8** (phase1.md, WP27), +10 | the shortcut reader: `.boot2` until wave 2, `.boot3` from then on |
| **boot-17**, kmain_o as a table (phase1.md, WP6) | no longer a byte item. Its −30..−60 were blob bytes only while kmain_o is in the blob. It rides wave 4 for its diagnostics, if wanted |
| **WP6** (phase1.md) | boot-7's −41 is still worth taking, but is no longer load-bearing. boot-18 (BR-154) lands in wave 0. boot-4 and boot-10 are wave 4's preconditions, not waves 0-3's |
| **WP7** (phase1.md), `.ovl` −45 | still lands. With wave 3's 176, `.ovl` covers the +93..+103 wants (WP8 install and gfce-6, BR-18, BR-88, BR-141, BR-157, BR-158, BR-25) with about 120 to spare, **on kern_big and on kern_mega** |
| **rt-7**, the keyboard mouse (the review's item 6; followup-decisions-6-8.md) | **wave 5**, in the owner's shape: −310 static resident on kern_big, kern_emu and kern_small, −306 conventional on kern_mega. It no longer depends on rt-2's boot cache or on any file |
| phase1.md's "14th sector or boot-17" choice | **gone.** The blob wants (+66..+85, or +101..+160 with BR-6's alias form) fit after wave 1 |

## 12. Risks, and what catches each

| # | risk | silent? | caught by |
|---|---|---|---|
| R1 | a body reached from the load-time cone runs packed bytes | yes, and it can show on one geometry only | `os88ovlchk` B2; `splashbar`, `splashspin`, `kzboot` |
| R2 | a missed re-pointer: the slide, the ROM or the HMA leaves a `.boot3` COLD_SEG word stale (a crash on mono after the slide; a call that works by accident after HMA adoption) | yes | `os88cfx` and `os88rom` refuse an unclassified site; `rp_walk`'s look pass; `coldslide`, `rombig`, `romsmall`, `romnodisk`, `hmacold`, `hmadrv`, `hmahiber` |
| R3 | a call after the drop (resident code, a module, a stored pointer) | yes | `os88ovlchk` B1, B3 and B9; the `b3poison` knob row |
| R4 | the packed read, or `.boot3b`, overlaps the blob | it dies at once | the derived lift; boot2.asm:546; the new `.boot3b` and NOKZIP read-end guards (section 13) |
| R5 | guard 5 blind to the lift (BR-154, already there today) | yes | wave 0's fix on every kernel; `small128`, `bootfloor`, `bootfloor-ab` |
| R6 | the two-pass identity: a KZ number or a loader label inside `.boot3` | no (the check refuses it) | `os88ovlchk` B6 |
| R7 | CS-addressed data left behind (`ovc_buf`) | yes, and it corrupts the blob | `os88ovlchk` B5; wave 2 moves `ovc_buf` with its users |
| R8 | a near call between `.boot2` and `.boot3` | yes | `os88ovlchk` B4. MARKW under BOOTMARK=1 near-calls `kmo_mark`, so a body containing one cannot move until MARKW has a `.boot3` arm |
| R9 | `.vgabuf` overlap: a live entry in the first 336 bytes, rewritten by the planar decoder on a VGA machine (kern_big, kern_emu) | yes, and VGA only | the `b3_live` guard; `vgadrop`, `coldpic` |
| R10 | a `.boot3` block in a file kern_dos includes | yes | `os88ovlchk` B7; `kerndos` |
| R11 | installed hard disks: the VBR's BLOB_SEG goes stale on each lift step | it fails at boot | already there today. `install`, `hdtakeboot`; say so in SPEC.md 52.10 |
| R12 | kern_big's boot arena at 128KB shrinks about 1.5KB by wave 2 and recovers by wave 4 | no | no row, by the owner's decision (owner, 2026-11-08); `kernsize`'s host figure (section 13); `bootfloor-ab` is floor-relative and so invariant |
| R13 | a tool that does not know the section resolves a `.boot3` label against the blob | sometimes | `os88sym` raises on an unnamed section; the others are fixed in wave 0. `os88ladder --selfcheck` is on demand |
| R14 | NO_ROM_COLD and NO_COLD_SLIDE overflow their blob once wave 1 lands | no (`%error`) | the per-arm placement in section 5; `buildmatrix` |
| R15 | wave 5: a resident entry reaches an unarmed or dropped slot (`mod_gone` answers CF = 1, which `kbm_key`'s callers read as "spent", so every key would be eaten) | yes | the gates test the ROW, not `[mou_ptr]`; FLAT_PAYLOAD and a refused adoption leave the row 0; the new `kbmadopt` row (section 13), broken on purpose by gating on `[mou_ptr]` instead |
| R16 | wave 5: the drop frees the claim while a task is still inside it. `[mou_ptr]` is set from interrupt or worker context, so the drop is deferred to `.have` on the UI task; `osapi_mouse` stops entering the keyboard mouse once `[mou_ptr]` is set (mouse.inc:1559), but a task other than the UI task (a worker) preempted inside `kbm_move` at that instant would resume in freed RAM. The path such a task can be on (`kbm_poll` → `kbm_key` → `kbm_move`/`kbm_btn`/`kbm_rclick`: `cur_move`, `sch_wake_ui`, `evq_push`) makes no blocking call; `gfx_lock` is only on `kbm_ui`'s raise path, which only the UI task runs. So a task is inside the image only if it was preempted there, and it leaves on its next slice | yes | the same exposure every module drop carries (mod.inc's `mod_drop` banner; BR-4's DESK.DRV race). Before the commit, confirm that no worker calls OSAPI_MOUSE on a mouseless machine; if one may, defer the drop until every task has been scheduled once since `[mou_ptr]` was set. One more UI pass alone does not bound it |
| R17 | wave 5: the image grows past 512 bytes and silently takes two granules (about +700 net on a mouseless machine) | yes | the `%error` at 512 |
| R18 | wave 5: the `[rom_mfp]` swap is left installed, or does not forward ROMF_FIX, so a ROM machine's later module loads read `.boot3m`'s dead RAM | yes | B3's registered exception, restored in the same routine; `romsmall`, `rombig`, `hmadrv` with a mouseless profile |
| R19 | wave 4 spends a stage-2 call after the bar on 360KB/720KB (section 10.2) | no, once `blobruns` is fast | `blobruns` (fast), its STAGE2 row |
| R20 | kern_small's derived lift moves the blob onto the 64KB page, re-creating a stage-1 split it was meant to remove | yes | `blobruns` once it models the page bound (section 13); `smallboot` |

**Found on the way, and not this plan's to fix.**

- By map arithmetic, kern_small's blob straddles the 64KB DMA page: linear
  0xF200..0x10800, with the page bound 7 sectors in. Stage 1 bounds its runs
  at the page (boot.asm:406-418), but `t_blobruns`' stage-1 model does not.
  So small360 probably pays 3 stage-1 calls (6 + 1 + 4), not the 2 the row
  reports (PREDICTED). Confirm the count with `tools/os88boot.py` on small360
  before acting on it; section 3.3's page term is the fix if it holds.
- **`t_blobruns` models the wrong blob length on two kernels** (found by this
  revision). Its `boot2_secs()` reads the literal `13` off
  `BOOT2_SECS  equ 13 + BOOT2_KNOBSEC - BOOT2_SMALLSEC`, so it prices
  small360 and small720 at a 13-sector blob when kern_small's is 11
  (BOOT2_PAD 0x1600 in build/smallk/kernel.map, MEASURED), and would price
  kern_emu's 14 as 13. Its IMAGES comment ("6 + 7 at 13") records the wrong
  length. The fix reads each kernel's own `blob` field from its
  kernel.kz.json (5,632 on kern_small: 11 sectors).

## 13. Gates and rows

**Assembly-time guards** (kernel.asm, at the foot where the `.cfx` guards are
now), on every kernel whose `.boot3` is on:

- `.boot3` (then `.boot3m`) is the file's last section:
  `MODC_START = B3_START + B3_SIZE` (+ `.boot3m`).
- `B3_RAM*16 + B3_SIZE + B3_RES <= BLOB_SEG_AT*16`, the nobits scratch.
- NOKZIP's plain read, rounded as the read rounds it: the whole file past the
  blob, not `.boot3` alone, so
  `KERNEL_SEG*16 + ((MODC_START - BOOT2_PAD + 511) & ~511) <= BLOB_SEG_AT*16`.
  (The first draft's `B3_RAM*16 + roundup512(B3_SIZE)` rounds the wrong
  length: B3_RAM is paragraph-aligned, not sector-aligned in the read.)
- On wave 2's arms, `.boot3b` starts on a linear 512-byte boundary
  (`(B3_RAM*16 + ovc_buf) % 512 == 0`), since `ovc_buf` is a disk-read
  destination.
- `b3_live - $$ >= KVGABUF_SIZE` (COLD_SLIDE arms).
- `B3_SIZE + B3_RES <= 0xFFF0`.
- `KZ_MARGIN_K == KZ_MARGIN`.
- Guard 5 and `KS_BOOTMX` on BLOB_SEG_AT, on all four kernels.
- Wave 5: the `.boot3m` image is at most 512 bytes; its header's entry count
  equals `MODFP KBFP`'s.
- boot2.asm:546 is unchanged.

**Host gates**, new or extended:

- `os88ovlchk` B1-B9, with fixtures, on every kernel's build.
- `os88cfx`'s fourth group, and `.boot3m` classified as a module image.
- `os88rom`'s `rt_b3` on kern_big and kern_small, `.boot3m` cut as a module
  image's list for ROMF_FIX, and `romtool --selfcheck`
  (`model_adopt`/`model_hma`).
- `t_blobruns`, **moved to the fast tier (owner, 2026-11-08)**, 0.03-0.05 s
  MEASURED (section 10.2):
  - each kernel's own blob length, read from its kernel.kz.json, not the
    literal 13 (section 12);
  - the stage-1 page bound;
  - `--tail N` to price N extra packed sectors;
  - kern_mega rows, track-bounded as a 286 reads, so that its thresholds are
    ratcheted rather than predicted.
- `t_bootfloor`: the injected HEAP_PARA and BLOB_SEG equal the map's
  BLOB_SEG_AT, which is what `B2CALL` rests on.
- `kernsize`'s `boot3=` line, and the **boot arena at 128KB** on its `boot`
  line, per kernel: 128KB less the blob's nobits top. It needs one more
  field on the `-DKERNSIZE` `%warning` (`b2res=`), which changes no byte of
  the kernel. This is the host-side figure that replaces a kern_big 128KB row
  (owner, 2026-11-08).
- In wave 4, the `tests/blobbodies.txt` ratchet, per kernel.
- In wave 0, a `cmp` of `build/smallk` and `build/megak` `kernel.bin` against
  the starting commit, at one commit.

**Rows to NAME** for the single final pass. Run them as one deduplicated
`os88test.py soak -k ...` union, then `test-full` once.

Tiers as tests/suite.py registers them (the first draft filed thirteen soak
rows under fast or full; corrected by the verifier), with `blobruns` moved:

- **fast:** `ovlchk`, `canary`, `romtool`, `image`, `diskverify`,
  `livefull`, **`blobruns`** (moved by wave 0).
- **full:** `small128`, `bootsmoke`, `megahigh`, `ps2mouse`.
- **soak, host-only and cheap** (run by hand as each wave's host checks,
  then again in the union): `bootfloor` (3.5 s), `coldpic`
  (`os88romfix.py --check`), `resident`.
- **soak, emulator:** `coldslide`, `rombig`, `romsmall`, `romnodisk`,
  `hmacold`, `hmadrv`, `hmahiber`, `smallboot`, `vgadrop`, `buildmatrix`,
  `kzboot`, `kzboot-off`, `hdboot`, `hdtakeboot`, `install`, `cylrun`,
  `splashbar`, `splashspin`, `bootstatus`, `blobsum`, `bootfloor-ab`,
  `autostart`, `hibernate`, `fontpick`, `desksc`, `postboot`, `heapmap`,
  `v20boot`, `pravetzboot`, and for kern_mega `megaovl`, `megamod`,
  `megahiber`.
- **item E adds** nothing new: `splashbar`, `splashspin`, `kzboot`, `canary`.
- **wave 4 adds:** `vesablitd`.
- **wave 5 adds:** `usbmouse` (a mouse that first speaks after the boot, through
  USBMOUSE.DRV's feed), `vmmouse` (kern_emu, never adopted),
  `wirefleetnomouse` and `kdnomouse` (driven on the keyboard mouse),
  `curshape`, `hibernate` and `megahiber` with a mouseless image, `small128`.
- **new:** `b3poison` (soak, private tree, knob, on kern_big and kern_small);
  **`kbmadopt`** (soak, MartyPC, a mouseless profile): read MOD_KBM's row live
  and its claim at 512 bytes; open a menu and pick an item on the keypad and
  Space; then bring a serial mouse up, move it, and see the row go to 0 and
  the claim freed. Break it on purpose twice: gate `kbm_key` on `[mou_ptr]`
  instead of the row, and remove the drop; the row must go red both times.
  There is no keyboard-mouse row in the suite today (a search of
  tests/suite.py for one finds only rows driven on it).

## 14. SPEC.md sections to update first

- **SPEC.md 2.9:** a new subsection, "`.boot3`: the boot's third home". It
  covers the lifetime table of section 3.2 per kernel, the contract and the
  macros, the arms, the fences, the derived lift with kern_small's page term,
  and the name's one rule (a home, not a stage).
- **SPEC.md 2.9.6:** "one transient" becomes three homes, with the question
  each one answers.
- **SPEC.md 2.5.3 and 2.5.3.1:** the rules per half gain `.boot3`'s
  deadline, which is the drop, not the mount.
- **SPEC.md 2.5.3.2:** OVBCALL/OVSCALL arms, and B3SECT's, until the fallback
  is deleted.
- **SPEC.md 2.5.3.3 and 2.5.3.3.1:** where kmain_o's body lives; why the
  knob arms no longer need the knob sector for moved bodies.
- **SPEC.md 2.5.1.1 and 2.9.5:** the blob's floor and the derived BLOB_LIFT.
  Guard 5 counts it on every kernel.
- **SPEC.md 2.9.5.3:** `[spl_fseg]` now also says `.boot3` is aboard.
- **SPEC.md 2.9.13.1:** the file's parts, with `.boot3` and `.boot3m` last,
  per kernel.
- **SPEC.md 2.9.14 and 2.9.12:** `.boot2` is the loader and the splash, the
  non-splash residue is a ratchet, and the blob lengths after wave 4.
- **SPEC.md 15 and 15.3:** the first tick after the plain head (item E).
- **SPEC.md 15.3.8.5:** `blobruns` in the fast tier, and what its ratchet now
  models.
- **SPEC.md 18.93.1:** the canary's sector after wave 4, from boot-10's
  generator.
- **SPEC.md 2.8, 2.8.1 and 2.8.3:** a module row ADOPTED FROM MEMORY:
  MOD_KBM, no file, the one-call door, and a drop owned by the first pointer.
- **SPEC.md 2.10.4, 2.10.5, 2.10.7 and 2.10.9:** `rt_b3` on both ROM kernels,
  the HMA kind walking it, the slide's fourth group (`.cfx` subsumed), and the
  door that forwards ROMF_FIX.
- **SPEC.md 9.6, 9.6.1.1 and 9.6.6:** the keyboard mouse's two halves, where
  each lives, and why `[mou_ptr]` decides the adoption.
- **SPEC.md 50.2.1:** the granule a mouseless machine pays.
- **SPEC.md 87.3 and 87.9:** hibernate carries the adopted claim as it carries
  any module.
- **SPEC.md 24.5.7:** kern_small's system disks after wave 4.
- **SPEC.md 39.22:** the `.vgabuf` overlap.
- **SPEC.md 52.10:** the VBR-refresh hazard.
- **docs/KERNEL-MEMORY.md**, "The boot overlay" and "Cold code": the third
  boot-only home. `.boot3` is NOT `.cold`: it costs boot RAM and load time,
  never a resident byte. Wave 5's claim is heap, and a mouseless machine's
  only standing cost.

## 15. Open questions for the owner

**All answered (owner, 2026-11-08, third round):** question 2 - fine, the plan
ships as one deliverable so a temporary loss between waves does not matter;
question 8 - move the clock ladder whole; question 9 - the 5-byte
`call BLOB_SEG_AT:x` with the `t_bootfloor` fence. The text below is kept as
the record of what was asked.


Questions 1, 3, 4, 5, 6, 7 and 10 were answered (owner, 2026-11-08) and are
folded into the body: the name (section 1), kern_mega (sections 5.2 and 8,
wave 3), kern_small (section 5.1), no kern_big 128KB row (sections 6, 9 and
13), wave 4 (section 8), the earlier bar (section 8, item E, and section
10.1), and `blobruns` (sections 10.2 and 13). Three stay open, under their
old numbers:

2. **Wave 2's boot-time RAM.** Is wave 2's lift of about 4 sectors
   acceptable? It costs 0 resident bytes and about 80 ms on 360KB after the
   bar. On kern_big the 128KB boot arena goes from 24,064 to about 22,528
   bytes until wave 4 gives it back (a host figure; there is no row); on
   kern_mega the lift is about 5 sectors on a 640KB floor.
8. **The clock ladder.** Should wave 3 move it whole (1,782, MEASURED) or
   only the two clock-card rungs BR-157 and BR-158 touch?
9. **`B2CALL`'s constant segment** (section 4). Take the 5-byte
   `call BLOB_SEG_AT:x` and the `t_bootfloor` fence, or pay `SPLCALL`'s 19
   bytes a site and keep "the blob is wherever `[spl_fseg]` says" true for
   `.boot3` as well?

## 16. Verification

### 16.1 The adversarial pass

An adversarial pass on 2026-11-08, against this tree's code and its
`build/kernel.map`, `build/smallk/kernel.map`, `build/megak/kernel.map` and
`build/*kernel.kz.json`. It booted nothing: every check is a reading of
source, a map, or a host model (`t_blobruns`' own functions). Everything
labelled PREDICTED above is still PREDICTED.

#### What was checked and held

- **No extra int 13h before the bar.** Stage 2 reads the 9-sector plain head,
  then the tail in runs, and calls `spl_tick` only after each tail run
  (boot2.asm `.head_next` / `.load_next`). `kz_all` runs after the last run,
  so `.boot3`, the end of the tail, is expanded only after the bar is up and
  is never callable earlier. Stage 1's read is BOOT2_SECS, which waves 0-3 do
  not change.
- **The read's end and the room under the blob.** 0x600 + 4,608 + R 18,432 +
  145 × 512 = 0x18200, against the blob at 0x18600: 1,024 bytes (kz.json:
  `r` 18,432, `ksecs` 154, `headsecs` 9). boothd reads the same span:
  `(ksecs − BOOT2_SECS)` sectors to KERNEL_SEG + KZ_RPARA, then `kz_hd`.
- **The headroom before a stage-2 call.** `t_blobruns.stage2_calls` gives 17,
  17, 14 and 16 extra packed sectors free at 360KB, 720KB, 1.2MB and 1.44MB.
  The 18th, 18th, 15th and 17th cost a call, and moving R by 0-2 sectors
  changes nothing. kern_small has 8 free on small360 and small720.
- **Map figures** (all MEASURED, and all as the plan stated unless listed
  under the changes below):
  - kern_big: `.boot2` 6,652 (0x19FC) and `.ovl` 5,119 (0x13FF); `.cfx` 530
    at 0x191A0; CFX_RAM = VGABUF_RAM = 0x17DA; KERN_END 0x1800; BLOB_SEG_AT
    0x1860; BOOT2_RES 0x1C00.
  - kern_small: `.boot2` 4,930 of 5,632; `.ovl` 1,474.
  - kern_mega: `.boot2` 6,883 of 7,168; `.ovl` 5,118.
  - The decoded `cfx_tab`: 93 + 3 + 25 sites in 192 bytes. The blob's 25 are
    `as_boot` 5, `hb_probe_x` 5, `ovl_sc_load` 4, `kmain_o` 2, `ovc_unpack`
    2, and 1 each in `font_init`, `ovl_sc_free`, `ovc_load`, `drv_boot_x`,
    `bdl_hma`, `bdl_publish` and `bvs_switch`. `.ovl`'s three are
    `mem_init_x` and `desk_init` twice.
  - Every wave-1, wave-2 and wave-4 span in section 8, recomputed from
    adjacent blob symbols. The stays-in-`.boot2` residue sums to 3,215, and
    the whole partition sums to 6,652.
- **Lifetime.** `kmain` runs `SPLGATE1 kmain_o`, then `xor ax, ax /
  mov [spl_fseg], ax / call COLD_SEG:mem_unblob_x` (kernel.asm:6106-6129).
  `[mem_base]` is written only by `mem_init_x`, `mem_unblob_x` and kern_mega's
  `mega.inc` after the switch, so no claim lands below the blob's nobits top
  while `.boot3` lives. `DSK_HCYLSEG` is above the blob (disk.inc:2946).
  kern_mega's `mega_enter` (6204) follows the drop.
- **Callers.** Every body in waves 1-2 has its callers in kmain_o or inside
  its own cluster (`drv_boot_x` ← kmain_o:5946, `hb_probe_x` ← 5975,
  `font_init` ← 5838, `as_boot` ← 6039). CTRL.DRV and HIBER.DRV expand their
  own instances (`cdl`, `cp_cgo`, `hbf`, `hbg`).
- **The re-pointers.** `cfx_slide` (MARK 10) runs before every wave-1 body
  that names COLD_SEG. The ROM's `rom_patch` runs at kmain_o's start, after
  `kz_all`. The HMA kind runs inside `drv_boot_x`. os88cfx and os88rom both
  fail today on a COLD_SEG site in a section they do not classify.
- **`bvs_apply`** is in the first tick (splash.inc:525-528), as the plan says.

#### What was changed in this file

1. **Wave 2's closure was wrong, and it is corrected.** The first draft
   moved `drv_boot_x` and its cone without noticing three things:
   - it near-calls `bvs_cgo` four times and passes it to `DRV_ATTACH bdl`
     as its hop. KGO_PAIR reads the inline `dw` through CS, so it must be in
     the caller's segment, and `.boot3` needs its own `KGO_PAIR b3`
     (+56, MEASURED from `bvs_cgo`/`bvs_kgo`);
   - it calls `bxf_sync`, `bvs_disp_init` and `bvs_switch`, which stay in
     the blob, or need doors;
   - it hands `bdt_*` titles to `ovl_spl_msg_drv`, whose `spl_mcs` reads
     `[cs:si]` in the blob. So `bdt_*` stays in the blob.

   The figures, the ledger rows for waves 2-4 and the lift arithmetic are
   re-derived. The lift still comes out at 224.
2. **kern_mega keeps BOOT2_RES** and `mem_init_x`'s non-small arm. They
   become B3-conditional, not retired. (Superseded by 16.2: with kern_mega
   on, the conditional disappears.)
3. **Section 3.3's lift formula** is restated in consistent units and checked
   against today's map (it gives 64 and the floor of 96 wins). It is scoped
   to the B3 arms, and the `extra` term of os88kz.py's R is named as the case
   the bound does not cover.
4. **Section 13's NOKZIP guard** rounded `.boot3`'s length instead of the read's,
   and is replaced. `.boot3b` gets a 512-byte alignment guard.
5. **`os88ovlchk` B3, B4 and B5** now name the transient CS hand-downs
   (DRV_ATTACH's `mov si, cs`, AS_WALK's ES buffers), the KGO hops, and blob
   offsets carried as immediates (`bdt_base`).
6. **`B2CALL`'s constant segment** is now stated as a new assumption against
   `[spl_fseg]`, with the 19-byte alternative. It is question 9.
7. **The ROM row:** `rom_patch` is kmain_o's second act, not its first, and
   the ROM code costs 14 bytes by encoding. os88rom must compare `.boot3`
   with `cfx_tab` blanked instead of skipping it.
8. **The `.vgabuf` overlap on a COLDHMA.SYS machine** lasts until
   `drv_boot_x`, not only "at home".
9. **The suite tiers in section 13** were wrong for thirteen rows: `blobruns`,
   `bootfloor`, the ROM and HMA rows, `coldslide`, `coldpic`, `smallboot`,
   `vgadrop` and `buildmatrix` are soak. As a result, **`t_blobruns`'
   STAGE2 ratchet does not fail `make`**. Section 10 said so, and the former
   question 10 asked for its promotion (answered in 16.2).
10. **Wave 4:** a one-call stage-1 blob on 360KB/720KB is 4 sectors, not 6.
    kmain's −4 drops `spl_gate`'s test, which is now said and registered.
    `dkb_geom`'s −4 is confirmed. The KSW_SECT span (614) and the clock
    ladder (1,782) are MEASURED, not PREDICTED.
11. **The 128KB arena** runs to the top of RAM, not to the boot sector, and
    the wording is fixed. Line citations are corrected (BLOB_LIFT is
    kernel.asm:1952).

#### What makes it infeasible

Nothing found. Every wave the verifier could check against the code is
buildable as corrected. Wave 2 is the one that changed shape (a 56-byte
second hop pair, `bdt_*` left behind, two doors), and it still frees about
1,640 blob bytes for 0 resident bytes and 0 calls before the bar. Open, and
not refutations: whether `bxf_sync`'s EXF_SYNC closure certifies, for which a
door is the fallback.

### 16.2 The owner's second round (the same day)

The revision applied the owner's answers and checked each new figure against
this tree's three maps, their kz.json files, the shipped images and the
source. It booted nothing and ran no emulator; the one program it ran was
`tests/unit/t_blobruns.py` (a host check), and t_blobruns' own functions as a
library for the walks.

**Measured or read off artefacts.**

- `t_blobruns.py`: 0.032, 0.037 and 0.052 s wall over three runs, rc 0.
- kern_small's ladder: `.cold` at file 0xA940, 0x5227 long, COLD_RAM 0x994,
  so its end is linear 0xEB67; KERN_END 0xEC0; BLOB_SEG_AT 0xF20; BOOT2_PAD
  0x1600 (11 sectors); no `.cfx`, no `.vga`, `.vgabuf` 0 bytes.
- kern_mega's: `.mega` at file 0x1C600, 0x5C10 long, ending at linear
  0x20C10; its packed read ending at 0x20E00; no `.cfx`.
- The keyboard mouse's spans, identical on all three maps: 388 bytes moving,
  and the state, shims and callers in kernel/mouse.inc, ui.inc, menu.inc and
  vmmouse.inc as cited in wave 5.
- kern_small's and kern_mega's `.boot2` and `.ovl` partitions, from each
  map's per-section symbol list (not the interleaved one, where `.lowbss` and
  `.vgabuf` labels fall among the blob's).
- `mod_need`'s ROM-first door, `mod_check`'s arming, `mod_hi`/`mod_hiboot`,
  `mod_free_row`'s kern_mega arm and `MOD_HIOK` (kernel/mod.inc), and
  `mou_hotplug`'s `.have` (mouse.inc:1380), which the wave-5 mechanism reuses.

**Derived (PREDICTED).** The first-tick table (section 10.1) and the
stage-2 thresholds at the wave-4 blob lengths and with boot-4's head
(section 10.2), walked over the shipped images with t_blobruns' `runs()` and
`stage2_calls()`; every lift, headroom and arena figure for kern_small and
kern_mega; wave 5's glue and image sizes, by encoding.

**What changed in this file.**

1. Generic: sections 2, 3, 5 (with 5.1-5.3), 6, 9 and 13 per kernel; the
   fallback arm becomes transitional; B9 added.
2. kern_small's verdict: it does not need the room and opts in for the
   ladder, wave 5's resident bytes and wave 4's shorter blob.
3. The name kept, with the reason stated as the owner's test and the one
   trap closed (`kernel/boot3.inc`, "home, not stage").
4. Wave 4 is in the plan, with its preconditions and its earlier bar
   quantified. The first draft's "5-6 fewer sectors before the bar, about
   110-130 ms" counted only the blob: the first tail run moves with it, so on
   360KB/720KB the bar comes about 18 sectors (~0.4 s) sooner, and on 1.2MB,
   1.44MB and small360 no sooner.
5. Item E (one tick after the head) is compared with wave 4 and recommended.
6. Wave 5, the keyboard mouse, priced per kernel, with hibernate and
   kern_mega covered.
7. The kern_big 128KB row is gone; its arena is a `kernsize` figure.
8. `blobruns` goes to fast, and two gaps in its model are named: the literal
   13 it reads for every kernel, and the stage-1 page bound.
9. Wave 4 is found likely to cost a stage-2 call after the bar on
   360KB/720KB (R19), within a sector either way.

**What makes it infeasible.** Nothing found. Two things to check before
wave 5's commit, neither a refutation: that `mega_modhif`'s code half can be
read through `cs:` (else `kbm_tab` stays in DS on kern_mega), and that no
worker calls OSAPI_MOUSE on a mouseless machine (R16).

### 16.3 The second adversarial pass (the same day)

A second verifier checked the revision against this tree's code, the three
maps and kz.json files and the shipped images. It booted nothing and ran no
emulator. It ran `tests/unit/t_blobruns.py` (five runs: 0.029 to 0.049 s
wall, rc 0) and t_blobruns' own `runs()` and `stage2_calls()` as a library.

#### What was checked and held

- **The first-tick table (section 10.1)**, re-walked cell by cell over the
  six shipped system disks, with kern_small's real 11-sector blob, with and
  without a track-bounded head: every cell matches. So do the stage-2
  thresholds (17/5 on 360KB and 720KB, 14/20, 16/22, 8/10 on both small
  disks) and the boot-4 variants (0/6, 15/21, 17/23, 9/11).
- **Stage 1's DMA-page bound** (boot.asm:406-418) is real, so small360's
  6 + 1 + 4 stands as PREDICTED, and a 7-sector blob at 0xF200 needs no
  page split while one lifted to 0xF600 needs one: the page term is right.
- **`blobruns`' timing and tier.** 0.03-0.05 s and it boots nothing; it reads
  the six system disks and two kz.json files, which `all` builds. The fast
  tier, in WRITING-TESTS 2.1's *shipped artifacts* family beside `image` and
  `canary` (both fast), is right. Its literal-13 gap is real: it walks
  small360 and small720 at 6 + 7.
- **Item E.** The head loop ticks nothing, the tail loop ticks after each
  run, `spl_tick` clamps and stores any count (splash.inc:237-241), and the
  canary's `.rerun` re-enters at `.reload`, above the head loop, so it
  re-ticks at 0. The comparison with wave 4 is honest, and the
  recommendation (both, item E first) stands.
- **Wave 4's preconditions.** phase1.md WP6 (lines 880-924) makes boot-4 and
  boot-10 gates on wave 4's shrink. boot-10 is needed outright (the canary
  sector is a function of BOOT2_SECS, and BR-1's 57 was checked at 13-15
  only); boot-4 is needed because it moves the same function's other input,
  KZ_HEADSEC, so the generator is run once against the final pair.
- **The kern_small verdict.** Its residue is 1,533 bytes (MEASURED, adjacent
  symbols of build/smallk's `.boot2`; the plan said 1,534), and every
  candidate (`sched_init` MARK 4, `mem_init_x` 11, `font_init` 13,
  `mouse_init` 18, `snd_init`) runs after `rom_patch`, kmain_o's second act,
  so the ROM kind's `rt_b3` re-points them in time. Opt in, as recommended.
- **The keyboard-mouse split.** The 388 bytes are exact on all three maps.
  No moved routine is reached from an ISR: `kbm_isr` and `KBM_SCAN` only set
  `[kbm_p5]` and call `kbm_shf`, `kbd_track`, `sch_wake_ui` and
  `kbd_ovflow`, all resident; `mou_isr` shares only `mou_evfill`. The
  callers of the moved half are exactly ui.inc:214 (`kbm_key`), ui.inc:722
  (`kbm_ui`) and `kbm_poll0` (`kbm_key`, `kbm_p5spend`); every other user
  (`menu_track`, `ui_drag`, `ui_grow`, `osapi_mouse`, WIN95.DRV's WNK,
  STIKMENU.DRV's SMK) calls `kbm_poll`/`kbm_pollm`, which stay. On a machine
  with a mouse, after the drop, the row is 0 and the stub answers
  `kbm_pass` (CF = 0, registers kept), which is what today's `[mou_ptr]`
  test answers. kerndos includes no part of mouse.inc. The four shims the
  plan says exist (`cw_mou_clamp`, `cw_gfx_lock`, `cw_gfx_unlock`,
  `cw_wm_hit`) are on all three maps; the five it adds are on none.
- **The claim.** `mod_need` asks `[rom_mfp]` for SIZE first and skips the
  disk on CF = 0, copies by COPY, arms from the header, and then fixes by
  `mod_cfix` (COLD_SLIDE) or ROMF_FIX (kern_small); `mod_hi` waits for
  `[mega_pm]` and `mod_hiboot` moves every boot-loaded row at the switch;
  `mod_free_row` frees an above-1MB pair. The granule is 512.
- **Hibernate.** Resume restores the mouse state with the image and resets
  neither `[mou_ptr]` nor `[mou_seen]`, so a session comes home with its own
  answer, as it does today.

#### What was changed in this file

1. **The drop costs 11 resident bytes, not 7.** `.text` has no KGO hop, so
   `mou_hotplug`'s `.have` far-calls a 6-byte `.cold` door. The glue is 78
   (82 on kern_mega); the net is **−310** on kern_big, kern_emu and
   kern_small and **−306** conventional on kern_mega (−282 with the table in
   DS); a mouseless machine pays **+202** (+206 on kern_mega when `mod_hi`
   refuses). Section 5.1, wave 5's price table, the ledgers and section 11
   follow.
2. **kern_mega's door word** is initialised `.text`, seeded
   `COLD_SEG:mod_gone`, not `.bss`; and `mod_need`'s two ROM-first arms are
   about +16 of `.cold`, not +3.
3. **The adoption reaches `mod_need` through a KGO hop**, `b3_cgo`; wave 5
   brings `KGO_PAIR b3` itself when it lands before wave 2, and on
   kern_small (56 transient bytes).
4. **`kbm_tab` is read at five sites, not four**; the exits become `retf`
   with one `push cs` for `kbm_btn`'s second contract, and no door.
5. **Every `[mou_ptr]` setter is listed** at the drop, each with
   `[mou_seen]` beside it.
6. **R16** is sharpened: the path a non-UI task can be on makes no blocking
   call, so the fix if one may call OSAPI_MOUSE is to wait one scheduler
   round, not one UI pass.
7. **Item E's precondition** is kern_big's alone, and its tick sits inside
   `%ifndef BOOT_NOSPLASH`.

#### What makes it infeasible

Nothing found. The two checks 16.2 left for wave 5's commit stand
(`mega_modhif`'s code half readable through `cs:`; no worker calling
OSAPI_MOUSE on a mouseless machine).

### 16.4 Wave 0 as built (build, 2026-11-08)

Built against this tree's code; where the code disagreed with the plan the
code won, and each deviation is listed here. Host checks only (CLAUDE.md's
Testing rule): no emulator ran.

**What was measured.** kern_big: `.boot3` 535 bytes at file 0x191A0
(`.cfx` was 530), `B3_RAM` 0x17DA, BLOB_LIFT 96 (the derived bound is 64 and
the floor wins), `kernel.bin` 103,351 (+5), KERNEL.SYS 85,184 (+16, 167
sectors, unchanged), the kernel's `.text`, `.ovl`, `.cold` and `.vga`
byte-identical to the starting commit, and the blob different only in three
`call B3_SEG:` offsets (`cfx_grid`, `cfx_ctxcap` and `cfx_cfgname` moved 5
bytes down the section). `cfx_tab` 195 of 256 bytes (93 + 3 + 25 + 0).
`kernsize`: guard 5 headroom 22,016 (it read 23,552, the lift uncounted; kern_small 62,464 -> 60,928, kern_mega 509,440 -> 507,904),
boot arena at 128KB 24,064, `.boot3` 523 bytes before the lift steps;
kern_small 60,928 and 63,488; kern_mega's arena 509,952 at 640KB.

**Deviations.**

1. **The fourth group is `mov ax, cs`, not `mov ax, B3_SEG`**: two bytes
   against three, so `.boot3` is `.cfx` + 5, not + 6. DS and CS are both
   `B3_SEG` at that point.
2. **B3_ON is COLD_SLIDE in wave 0** - kern_big and kern_emu, where `.cfx`
   was. The NO_COLD_SLIDE and NO_ROM_COLD knob arms (section 5) take the
   fallback, as kern_small and kern_mega do, until wave 1 moves a body:
   they had no `.cfx`, nothing moves in wave 0, and turning the section on
   there would change two knob layouts with nothing in them that only
   `buildmatrix` (soak) could see.
3. **`.boot3b` is not created yet.** `B3_RES equ 0`, and every guard and
   term that names it is written; the nobits section arrives with its first
   user, `ovc_buf`, in wave 2.
4. **The macros take a kind word.** `B3SECT [ovl|ovb]` and
   `B3CALL x [, ovl|ovb]` carry the body's old home, so the fallback arm is
   chosen at the body and at each call; rule B8 holds the two words to each
   other. `B2CALL`'s fallback is BLOBCALL (the plan's column said "-"): in
   that arm the caller is in the blob.
5. **`os88ovlchk`'s walk no longer reads a `section` line inside a `%macro`
   body as the definition's switch.** B3SECT's four arms are four such
   lines, and read that way they filed the rest of kernel.asm in `.boot2`
   (rule 2c reported `kmo_mark` stale). `tools/os88modcost.py` had the same
   defect in its source scan and its listing pass and is fixed the same way.
   `os88modcost --ovl` was already failing at the starting commit (`ovl`
   attributed 3,441 of 5,119) and still does: not this wave's, not fixed.
6. **Rule exceptions are markers on the line** for B3 (`; ovlchk: B3
   hand-down - why`) and B5 (`; ovlchk: B5 blob offset - why`); B1 uses
   `tests/ovlrefs.txt` as planned. The expander behind B1-B9 evaluates
   `%ifidn` on a macro's known arguments (VSW_SWITCH's `.boot3` call is
   `bvs`'s alone; CTRL.DRV expands it as `vid`), and keeps every arm of any
   other condition.
7. **The fixtures run inside `os88ovlchk` on every invocation** (18 trees,
   ~0.02 s), not as a row: a rule whose fixture stops failing fails `make`.
   The rules cost ~0.45 s, paid back by a `%` byte test in front of the
   eighteen loops' macro regexes, so the tool's CPU is unchanged (4.3 s
   against 4.3 s at the starting commit; the `ovlchk` row's registered 1.4
   s is stale on this box at both).
8. **The `t_bootfloor` fence is a fast row of its own, `bootfence`**
   (`t_bootfloor.py --fence`): `bootfloor` is soak and re-assembles the
   kernel (3.5 s), and the owner asked for a BUILD-time fence. It reads the
   sectors' encodings against each tree's map, 0.04 s. `t_bootfloor`'s own
   `boot2_secs()` had the literal-13 defect too and now reads the map.
9. **`kernsize`'s new fields are a second `%warning`, `ksb:`**, because its
   parser reads the first `ks:` line only. `b2res=` is there with `boot3=`,
   `b3ram=`, `b3res=` and `blift=`.
10. **`blobruns`' small360/small720 ratchet moves 2 -> 3.** That is the
    model fixed, not a change: at kern_small's own 11 sectors and with the
    page bound, stage 1's read is 6 + 1 + 4. PREDICTED until `os88boot` on
    small360 confirms it in the final pass. `--tail N` is added. **kern_mega
    rows are not**: they need `make mega`'s images, which `all` does not
    build and a fast row cannot rest on; they land with kern_mega's wave 1.
11. **`tests/coldslide.py` counts the fourth group and does not read it**:
    `.boot3` is the heap's by the time the desktop is up, so a live read
    needs a stop before `mem_unblob`, which matters once the group is
    non-empty (kern_big wave 1). **`tests/bootstatus.py` and
    `tools/os88boot.py` needed nothing** beyond a comment: nothing in them
    names the section, and `B3CALL cfx_slide` lists as an ordinary call.
12. **The SPEC section is 2.9.15** (2.9.15.1-2.9.15.4), with pointers from
    2.5.1.1, 2.5.3.1, 2.5.3.2, 2.9.6, 2.9.13.1, 2.10.3, 2.10.4, 2.10.9,
    15.3.8.5, 39.22 and 52.10.2.1.
13. **The fence covers kern_emu's tree when it exists** (verifier round 1).
    kern_emu is `B3_ON` too, so its B2CALLs rest on `build/emuk/boot.bin`
    and `boothd.bin`; `all` does not build that tree (`make emu` does), so
    `bootfence` checks it beside its own map whenever the map is there, as
    it does `build/smallk/`. Its BLOB_SEG_AT is kern_big's 0x1860 today.
14. **The verifier's round 1 also found four `.cfx` sentences left in
    SPEC.md** (2.10.9's slide bullet, the `cfx_grid` paragraph, the
    `vid_ctx_capture` row and `CFGN_STAGE`'s), now `.boot3`; and B1-B9's
    reports are de-duplicated - a site inside a macro expanded on every pass
    was reported once per pass.
15. **Rule B8 reads macro expansions, not only written lines** (verifier
    round 2). A `B3CALL` written inside a `%macro` body is never a written
    line - the stream gives it only as an expansion - so the written-lines
    test let two of today's four sites (`cfx_ctxcap` in VSW_SWITCH,
    `cfx_cfgname` in CFG_BOOT) carry the wrong fallback word unrefused,
    which from wave 1 assembles kern_small's or kern_mega's fallback into
    the wrong home. B8 now reads every expanded line but the B3CALL,
    B2CALL and B3SECT macros' own bodies, and reports at the expansion
    line naming the macro; an eighteenth fixture is a `B3CALL ..., ovl`
    inside a macro expanded in `.boot2`. Broken on purpose at both real
    sites: each now fails B8 (each passed before).


**Found on the way, not this wave's to fix.** `build/mega.img` and
`mega120.img` carry `build/syscheck.o88` - the SYSCHECK.O88 whose manifest
is written from kern_big's system disk (SPEC.md 118.2.1) - so they change
whenever kern_big's KERNEL.SYS or COLDHMA.SYS does, though kern_mega's own
kernel is byte-identical; `util360.img` carries the same file, by design.
Whether kern_mega's disk should carry a manifest of its own is SPEC.md
118's question.

### 16.5 Wave 1 as built (build, 2026-11-08)

Built against this tree's code; where the code disagreed with the plan the
code won. Host checks only (CLAUDE.md's Testing rule): no emulator ran.

**What was measured** (this tree's maps and kz.json, against the starting
commit built at the same commit count):

| kernel | resident | `.boot2` | `.ovl` | `.boot3` | lift (para) | KERNEL.SYS |
|---|---:|---|---:|---|---:|---|
| kern_big | 96,768, +0 | 6,478 -> 5,974 (-504) | 5,119, +0 | 535 -> 1,042 at 0x17DA | 96 (16 bytes before the step) | 85,184 -> 85,648 (167 -> 168 sectors; stage 2 154 -> 155) |
| kern_emu | 96,768, +0 | 6,091 | 5,119 | 1,042 at 0x17DA | 96 | 86,304 |
| kern_small | 58,880, +0 | 4,745 -> 3,310 (-1,435) | 1,474, +0 | 1,441 at 0x0EB7 | 96 -> **160** | 54,240 -> 55,408 (106 -> 109; stage 2 95 -> 98) |
| kern_mega | 62,976 (MEGA_RESIDENT), +0 | 6,709 -> 5,713 (-996) | 5,118, +0 | 1,010 at 0x2140 | 96 -> **160** | 115,888 -> 116,832 (227 -> 229; stage 2 213 -> 215) |

Guard 5's headroom at 128KB: kern_big 22,016 (unchanged), kern_small
60,928 -> 59,904; boot arena kern_big 24,064, kern_small 63,488 -> 62,464;
kern_mega 507,904 -> 506,880 at 640KB. `cfx_tab` 93 + 3 + 14 + 11 (197 of 256
bytes): the eleven `.boot3` words are wave 1's, which leave the blob's 25 at 14.
The ROM lists conserve: kern_big blob 25 -> 14 + b3 0 -> 11, kern_small blob
3 -> 1 + b3 0 -> 2. `.text`, `.cold`, `.ovl`, `.vga` and `.mega` are the
same LENGTH as the starting commit's on every kernel; their bytes differ only
in immediates that name blob offsets (`SPLCALL`/`SPLGATE1` targets in
`sch_isr`, `dkb_geom`, `dsk_xfer`, `mouse_init`, and `kbld_blob`), 5-8 bytes a
kernel - the blob's contents moved under them. No kernel is byte-identical,
and the plan claimed none for this wave. `blobruns`: every geometry's call
counts unchanged; small360/small720's three stage-1 calls are now 5 + 1 + 5
(the lift moved the page bound two sectors earlier), so the page term is still
PREDICTED and not acted on.

**Deviations.**

1. **The fallback arms are DELETED in this wave**, with `B3_ON`: every kernel
   switched on, which is section 4's condition. `B3SECT` takes no word,
   `B3CALL` one argument; the `%ifdef B3_ON` arms in driver.inc and vidsel.inc
   keep only their `.boot3` side. Rule B8 keeps "B3CALL at a non-`.boot3`
   target" and its B2CALL checks and drops the kind-word agreement; its two
   kind fixtures became a `.text` target and a blob target written inside a
   macro (18 fixtures still).
2. **kern_small moves by OVBCALL, not by `B3SECT ovb`**: its arm is `B3CALL`
   (was BLOBCALL) and every OVBCALL body's kern_small block says `B3SECT`. So
   ALL six OVBCALL bodies moved - including **`vid_probe_avail` +
   `vid_memchk`**, which section 8 did not list - and, under `BAKED_FONT`,
   `ovl_font_init` with its face. kern_big's arm stays `.ovl`. Rule 2d holds
   OVBCALL to `.boot3`; B4 no longer reads OVBCALL (both its arms are far).
   Every moved closure passed B1-B9 (`os88ovlchk`).
3. **kern_mega also took `cfx_grid`, `cfx_ctxcap` and `cfx_cfgname`**, the
   bodies `%ifdef B3_ON` already sent to `.boot3`: its blob's `bvw_rowcalc`
   DESK_ROWCALC copy, `bvs_ctxcap` and `ovc_cfgname` went. `mega_wrprobe`,
   `mega_gateb` and `mega_gwset` certified and moved. Its lift is 160, not the
   predicted 96..128: at `KERN_END` only 450 bytes fit under the floor.
4. **`kernel/boot3.inc` is %included twice**: its HEAD where kernel.asm
   declares `.boot3` (the slide prefix must be the section's first bytes, and
   every module's `B3SECT` block is met before the foot - `.boot2`'s `jmp
   boot2_entry` reason), its FOOT (`%define B3_FOOT`) at the foot for the
   `cfx_` bodies, which need the modules' macros, and the guards. The file-
   order guards (`.boot3` past `.cold`, `.ovl`, `.vga`, `.mega`) sit after
   `MODC_START` in kernel.asm, where `OVL_SIZE` and `MEGA_SIZE` are known.
5. **NO_COLD_SLIDE and kern_small start `.boot3` PAST `.vgabuf`'s bytes**
   (`VGABUF_RAM*16 + KVGABUF_SIZE` to the paragraph) instead of a zero pad: a
   `times KVGABUF_SIZE` at the head is a forward reference NASM refuses. The
   same RAM is left; kern_small's KVGABUF_SIZE is 0, so it is `.cold`'s end.
   A guard refuses an overlap with `.vgabuf` on any kernel without the slide.
6. **NO_ROM_COLD still does not assemble** - before this wave it failed on the
   blob and on `mou_p2knock`'s window guard; now only on the second, which is
   not `.boot3`'s. NO_COLD_SLIDE, which overran its blob at the starting
   commit, assembles. `BOOTMARK=1` (and `BOOTHALT=20`) assemble on kern_big,
   kern_small and kern_mega. Neither knob is in `buildmatrix`'s roster.
7. **`os88ovlchk` needed four rule repairs, each a false positive the move
   exposed**: B1 ignores quoted strings (bootprof's `"mouse_init"` label);
   B5 accepts `SPLCALL`'s own immediate (it reaches the blob through
   `[spl_fp]`:`[spl_fseg]`, which is right from any segment); rule 2c's
   stale-row check leaves rows naming `.boot3` labels to B1; and three
   `tests/ovlrefs.txt` rows register AS_WALK's labels, which the expander sees
   in kern_mega's `.cold` arm because it keeps every arm of `%ifdef KERN_MEGA`.
   The `mouse_init` row went (the probe left the blob and `.ovl`'s model).
8. **kern_mega `blobruns` rows were not added** (16.4 deviation 10 deferred
   them here): they still rest on `make mega`'s images, which `all` does not
   build and a fast row cannot. Undone.
9. **`tests/coldslide.py` reads the fourth group at a stop**: a first
   Hercules boot breaks on `spl_finish` (`BLOB_SEG_AT*16` + its offset; after
   the slide, before `mem_unblob_x`) and checks every `.boot3` word against
   the slid segment. UNRUN - it is a soak row - and its registered 35 s
   predates the extra boot, so the final pass re-measures it.
10. **`os88rom.py` prints the `.boot3` count** in its site summary
    (`93+3+14+11`, `47+2+1+2`). kern_small's ROM now walks `rt_b3` (its 14
    ROM bytes, `RT_HASB3` = 1).
11. **`kernsize`'s baseline was not re-blessed**: it was stale at the
    starting commit already, and blessing is a size pass's act.

### 16.6 BR-1, boot-7 and item E as built (build, 2026-11-08)

The three changes in `boot/boot2.asm` and their gates, as one commit:
BR-1 (docs/reports/big-review-2026-11-08/followup-canary.md, synth/bugs.md
BR-1), boot-7 (areas/boot.md) and item E (section 8). Host checks only
(CLAUDE.md's Testing rule): no emulator ran, no row was run but the fast
tier inside `make` and the host scripts named below.

**What moved.**

- **The canary, part 1**: `KSIG_OFF` 4,096 -> **22,528** (the Makefile and
  `boot2.asm`; the Makefile's arms make it 23,552 on kern_small and 22,016 on
  kern_emu, kern_mega and a knob build), **file sector 57** on every kernel,
  in the packed tail. Under KZIP the sum reads `mov si, KSIG_OFF +
  KZ_RPARA*16` (40,960 on kern_big, 34,304 on kern_small, 48,128 on
  kern_mega at this build) and the `%if KSIG_OFF >= KZ_HEADSEC*512` ES arm is
  gone. Three `%error`s fence it: the sector in the tail, inside the
  handoff's 64KB, and inside `KZ_SECS`; each was made to fire by hand.
- **The canary, part 2**: the plain head is read at the TRACK bound
  (`mov ax,[b2_spt] / xchg ax,[b2_runmax] / push ax` ... `pop word
  [b2_runmax]`, 12 bytes, under `%ifndef TRACK_RUN`).
- **Item E**: `xor ax,ax / mov dx, KZ_SECS-KZ_HEADSEC / call spl_tick` after
  the pop, inside `%ifndef BOOT_NOSPLASH` (8 bytes).
- **boot-7**: stage 2's copy of the diskette parameter table and its
  `DPT_AT` equate are deleted (41 bytes); stage 1's is the one copy.
- **`t_canary` rewritten** to walk the loader's own runs per kernel (blob,
  `KZ_HEADSEC`, R out of each kernel.kz.json; the head at the track, the tail
  restarting behind it, the DMA page), and to find the ARMED sector by the
  signature in each disk's own boot sector rather than by the Makefile's
  arithmetic. **Broken on purpose**: with `KSIG_OFF` back at 4,096 (the
  Makefile and `boot2.asm`) against the starting commit's images, which are
  armed at file sector 21, it is red on all six system images - the four
  9-sector ones because their run starts after the flip, the 1.2MB and
  1.44MB ones because sector 21 is in the head, which no longer crosses -
  and on both kern_mega images; restored, it is green: LBA 71, 71, 86, 90, 69,
  69, the follow-up's figures. Against a stale tree (sources at 22,528,
  images armed at 21) it is red naming the boot sector's signature.
- **`t_blobruns`' stage-2 model reads the head at the track**, and its STAGE2
  ratchet moves one call on four disks, with the reason beside it: 1.2MB 8 ->
  9, 1.44MB 7 -> 8, small360 and small720 7 -> 8; 360KB and 720KB 11
  unchanged (the head is one track there). PREDICTED, as BR-1 priced it.
- Records: SPEC.md 2.9.8, 2.9.13.1, 2.9.13.3 (retitled: the run-start rule
  replaces "the canary is unmoved"), 2.9.14, 15, 18.93.1 (file sector 57,
  the run-start rule, the blast radius as READS), 18.93.2's survey sentence;
  docs/KERNEL-MEMORY.md; docs/FIELD-NOTES.md 31 and a new **68** (the Pravetz
  report: what is known and what is not); `tests/rdiag.asm`'s header and
  docs/MARTYPC-DEBUG.md's clone-BIOS row (rdiag360 no longer tests a
  cylinder run); areas/boot.md's "write" claim; `tools/os88intguide.py`'s
  anchor on boot2.asm's `DPT_AT` (now on boot.asm's vector store; its
  `--selfcheck` and `os88ladder --selfcheck` both pass).

**What was measured** (each tree's maps and kernel.kz.json, against the
starting commit built in a worktree at the same commit count):

| kernel | resident | `.boot2` in the shipped KZIP build (map) | `.boot2` as kernsize prints it | `.ovl` | `.boot3` | KERNEL.SYS |
|---|---:|---|---|---:|---:|---|
| kern_big | 96,768, +0 | 6,148 -> **6,127** (-21) of 6,656, 529 free | 5,974 -> 5,933 (-41) | 5,119 | 1,042 | 85,648 (168 sectors), +0 |
| kern_emu | 96,768, +0 | 6,265 -> **6,244** (-21) of 7,168 | - | 5,119 | 1,042 | 86,304, +0 |
| kern_small | 58,880, +0 | 3,495 -> **3,463** (-32) of 5,632 | 3,310 -> 3,269 (-41) | 1,474 | 1,441 | 55,408 (109 sectors), +0 |
| kern_mega | 62,976 (MEGA_RESIDENT), +0 | 5,887 -> **5,866** (-21) of 7,168 | 5,713 -> 5,672 (-41) | 5,118 | 1,010 | 116,832 (229 sectors), +0 |

By encoding the parts are -41 (boot-7), +12 (part 2), +8 (item E) and, on
kern_small only, -11 (part 1's arm), which is what the maps show. Guard 5's
headroom, the boot arena and every `kernsize` line but `boot2`'s are
unchanged on every kernel.

**What was proven byte-identical, at one commit** (the starting commit in a
worktree under the scratchpad, `make`, `make mega` and `make emu`, against
this tree before committing): every section of `kernel-full.bin` but
`.boot2` - `.text`, `.ovl`, `.vga`, `.cold`, `.boot3`, `.mega` and every
module section - on all four kernels (the blob is the last thing in
`.boot2` and nothing outside it names a label past `boot2_entry`; `b2_cylok`'s
reader, `ovl_fdd_apply`, is in the blob). **KERNEL.SYS is identical past its
blob** on all four, so the packed stream did not move a byte and KERNEL.SYS
keeps its length. Of the shipped images, every apps, category, media, WIN16
and ARCADE disk is identical; the six system disks differ (the blob and the
boot sector's KSIG/BLOBSUM), `util360.img` differs only in `SYSCHECK.O88`
(the manifest of kern_big's system disk, section 16.4's note), and
`mega.img`/`mega120.img`/`emu.img` differ as their kernels' blobs do. The
boot sectors and `boothd.bin` (KZ_HD moved 21 bytes down) differ, as they
must.

**Deviations.**

1. **".rerun skips the head bound" needs no code.** The xchg/pop form puts
   back exactly what was in `[b2_runmax]`, which on `.rerun`'s second pass is
   the track, so the pass widens nothing and stays one-shot without a guard
   or a skip. One behaviour does change: a head read that exhausts its three
   attempts is now at the track bound, so `read_run` halts with "Disk error"
   instead of jumping to `.rerun` - on an 8088 it used to get a second set of
   attempts at the track bound, which is what the first set now is.
2. **Part 2's three instructions are under `%ifndef TRACK_RUN`**, where the
   bound is the track already: 0 on every shipped kernel.
3. **The break-on-purpose went red on six images, not four**: once the head
   is track-bounded a canary in the head never crosses, so sector 21 is also
   inert on the 1.2MB and 1.44MB disks under this loader. The four the plan
   named are the ones the OLD loader left inert.
4. **`t_canary` is an independent reader in a second way** the plan did not
   ask for: it reads the signature out of each disk's boot sector, so it
   walks the sector the build armed. kern_mega's images are walked
   model-only when present (`make mega` is on demand, so a tree that has run
   `make` since holds mega images armed for an older KSIG_OFF).
5. **rdiag was not given a cylinder bound** behind a define; its texts now
   say it no longer tests a cylinder run, and docs/MARTYPC-DEBUG.md points at
   the real kernel's `boot_cylrun`/`b2_cylok` instead. Undone, and recorded.
6. **No new fast row**: the canary row (`canary`) and blobruns carry it.

**Found on the way.** `kernsize`'s `boot2` figure is the **NOKZIP** arm: it
assembles without `-DKZIP`, so it leaves out `kz_all`, `kz_expand`,
`kz_hd` and the head loop - 194 bytes on kern_big at this build (6,127 in the
map, 5,933 printed). Every "free in the blob" figure this plan quotes from
`kernsize` is that much too generous; section 16.5's `.boot2` column is
kernsize's. Passing the KZ numbers (`python3 tools/kernsize.py -DKZIP
-DKZ_SECS=155 -DKZ_RPARA=1152 -DKZ_HEADSEC=9 -DKZ_NBLK=2`) prints the
shipped 6,127. Not fixed here.

**Rows to name** for the single final pass: `canary`, `blobruns` (fast,
both run inside `make`); `kzboot`, `kzboot-off`, `cylrun`, `splashbar`,
`splashspin`, `pravetzboot` (to be extended per FIELD-NOTES 68), `v20boot`,
`smallboot`, `hdboot`, `install`, `blobsum`, `fddpage`, `megacz` (soak);
`bootsmoke`, `small128` (full).


**Verifier round 1 (build, 2026-11-08).** Four stale texts fixed - the
Makefile's knob/kern_emu/kern_mega and kern_small `KSIG_OFF` arms and
kernel.asm's `BOOT2_SECS` knob paragraph still said "file sector 21", and
`tools/os88ladder.py` still named boot2.asm as a `DPT_AT` definer after
boot-7 - and one fact added: walked with `t_canary`'s own model over all eight
system images, the live tail sectors intersect in exactly {57} (last of its
band on 360KB/720KB, first on 1.44MB), so the new position has **no margin**.
The Makefile's `KSIG_OFF` block and SPEC.md 18.93.1 now say so, and that the
answer to a red `canary` row is a re-walked band, not a looser gate.

### 16.7 Wave 2 as built (build, 2026-11-08)

Built against this tree's code; where the code disagreed with the plan the
code won. Host checks only (CLAUDE.md's Testing rule): no emulator ran, no
row was run but the fast tier inside `make` and the host scripts named
below.

**What moved** (kern_big, kern_emu, kern_mega): `drv_boot_x`,
`drv_boot_want`; the SYSTEM.CFG reader's `ovc_*` expansion (`CFG_MAP`,
`CFG_DATA`, `CFG_SHARED`, `CFG_BOOT`) with `ovl_cfg_load` and
`ovl_fdd_apply`; `ovl_sc_drop` .. `ovl_sc_load` (desksc.inc); `DRV_ATTACH
bdl, b3_cgo`; `EXF_SYNC bxf, B3SWC`; and `KGO_PAIR b3` - all under `B3SECT`.
`ovc_buf` into `.boot3b` (`section .boot3b nobits vstart=B3B_AT`, kernel.asm;
`B3B_AT` = the next linear 512-byte boundary past `.boot3`, boot3.inc's foot).
Stayed in the blob, each reached by `B2CALL`: `ovl_spl_msg_cfg`/`_drv`/`_boot`
(their `ret` became `retf`), `bdt_*`, `xm_boot_x`, `bvs_switch`, `bdk_apply`,
`bvs_disp_init` through a new 4-byte door `bvs_disp_far`, and the loader's
`b2_cylok` through a new 6-byte door `ovl_fdd_cylok`.

**What was measured** (the shipped KZIP maps and kernel.kz.json, against the
starting commit built in a worktree at the same commit count; `kernsize`'s
`boot2` figure leaves out the 194-byte KZIP loader, section 16.6):

| kernel | resident | `.boot2` (map, KZIP) | `.ovl` | `.boot3` (+ `.boot3b`) | lift (para) | guard 5 headroom | boot arena | KERNEL.SYS |
|---|---:|---|---:|---|---:|---:|---:|---|
| kern_big | 96,768, +0 | 6,127 -> **4,489** (-1,638) of 6,656 | 5,119 | 1,042 -> 2,754 (+ 926) at 0x17DA | 96 -> **224** | 22,016 -> 19,968 | 24,064 -> 22,528 (128KB) | 85,648 -> 87,200 (168 -> 171 sectors) |
| kern_emu | 96,768, +0 | 6,244 -> **4,605** (-1,639) of 7,168 | 5,119 | 1,042 -> 2,755 (+ 925) | 96 -> **224** | 21,504 -> 19,456 | 23,552 -> 22,016 | 86,304 -> 87,872 (169 -> 172) |
| kern_mega | 62,976 (MEGA_RESIDENT), +0 | 5,866 -> **4,281** (-1,585) of 7,168 | 5,118 | 1,010 -> 2,670 (+ 914) at 0x2140 | 160 -> **256** | 506,880 -> 505,344 (640KB) | 508,928 -> 507,904 | 116,832 -> 118,400 (229 -> 232) |
| kern_small | 58,880, +0 | 3,463, identical | 1,474 | 1,441, identical | 160 | 59,904 | 62,464 | 55,408, identical |

The plan's prediction was about 1,640 freed and `.boot3` about +1,715 on
kern_big, lift 224, arena about 22,528; kern_mega lift about 256: all within
a few bytes. The lift is the read's term on all three kernels (`.boot3b`
fits under it), so the lift is 4 sectors of boot RAM (2,048 bytes) on
kern_big and kern_emu and 3 on kern_mega, as the owner accepted for this wave
(owner-decisions.md, third round, Q2); the arena falls 512 less than the
lift because the heap's floor came down by `ovc_buf`'s 512. KERNEL.SYS grows
3 sectors on each of the three; `blobruns`' stage-1 and stage-2 call counts
are unchanged on every disk (2/2/2/2/3/3 and 11/11/9/8/8/8). `cfx_tab`
93 + 3 + 3 + 22 (197 of 256 bytes): the eleven COLD_SEG words section 8
listed left the blob's 14 for `.boot3`'s 11, and the ROM's and COLDHMA.SYS's
lists conserve (93+14+11 -> 93+3+22); `os88rom.py kernel` cuts kern_big's
ROM with 266 bytes of its window spare.

**What was proven byte-identical, at one commit** (`make`, `make mega` and
`make emu` in a worktree of the starting commit, against this tree before
committing): **kern_small's `kernel-full.bin` section by section,
`kernel.bin`, `kernel.sys`, its boot sectors and `small360.img`/`small720.img`.**
On kern_big, kern_emu and kern_mega every module section (`.modc` .. `.modfs`)
and `.vga`/`.mega` are identical; `.text`, `.ovl` and `.cold` keep their
lengths and differ only in immediates that name blob offsets or the boot floor
- `sch_isr`'s and `dkb_geom`'s SPLCALL targets, `dsk_xfer`'s, `kbld_blob`,
`mouse_init`'s two, and `mem_init_x`'s `add ax, BOOT2_PAD/16` (it was
`BOOT2_RES/16`, 512 bytes higher) - 8 bytes a kernel; `.modmap` moves with
MODC_START. Of the shipped images every apps, category, media, WIN16, ARCADE
and kern_mega apps disk is identical; the four system disks, `emu.img`,
`mega.img`/`mega120.img` differ in KERNEL.SYS, BOOTHD.SYS, COLDHMA.SYS (its
lists) and ASSOC.DAT (cluster numbers: KERNEL.SYS grew), and `util360.img`
only in `SYSCHECK.O88` (section 16.4's note).

**Deviations.**

1. **`ovl_fdd_apply` read the loader's byte through the blob's CS** (`and al,
   [cs:b2_cylok]`), which the plan's closure missed: in `.boot3` CS is
   `B3_SEG`, and `b2_cylok` is a label past `boot2_entry`, which rule B6
   refuses (it moves between the two passes). The instruction is a 6-byte
   blob door, `ovl_fdd_cylok` (`and al, [cs:b2_cylok] / retf`), B2CALL'd in
   its place (5 bytes for 5). The body still moved (-93 net to the blob).
2. **kern_mega's `ovc_buf` moved into `.boot3b` too, and `BOOT2_RES` is
   gone on every kernel.** This wave's brief said kern_mega keeps BOOT2_RES
   and `mem_init_x`'s non-small arm, citing section 16.1's item 2 - which 16.2
   superseded (section 5.2: "with kern_mega on, the conditional disappears").
   The build settled it: with the buffer past kern_mega's blob its lift was
   already 256 (the read binds) and its arena 507,392; with the buffer in
   `.boot3b` the lift is still 256 and the arena 507,904 - the move is free
   there and gives back 512 bytes of boot RAM. `mem_init_x`'s two arms then
   assemble the same instruction on kern_big and kern_small (`add ax,
   BOOT2_PAD/16`), so the arm went with the equate, byte-identically on both;
   `kernsize`'s `b2res=` field now carries `BOOT2_PAD`.
3. **kern_small stays byte-identical**, so two places are armed rather than
   one-ladder: kmain_o's call is `B3CALL drv_boot_x` under `OS88_DRIVERS`
   and the blob's one-byte stub's `BLOBCALL` on kern_small; and the three
   status-line entries end in `SPL_MRET` (vidsel.inc: `retf` under
   `OS88_DRIVERS`, `ret` on kern_small, where nothing calls them - dead blob
   bytes kern_small has carried since SPEC.md 2.5.2, not this wave's to cut).
4. **`os88ovlchk` needed four repairs**, none a rule relaxed: rule 2c's
   registry (`tests/ovlrefs.txt`) gains the seven blob entries `.boot3` now
   reaches (the three status-line entries, `xm_boot_x`, `bvs_switch`,
   `bvs_disp_far`, `ovl_fdd_cylok`), each with its guarantee, and a
   `drv_boot_x` row for the one name in two arms; B5 no longer reads a
   line's own label as an immediate, and B8 accepts a `B3CALL` target any of
   whose definitions is `.boot3` (the same name-in-two-arms); `DRVBOOT`, rule
   2e's and B6's mount marker, recognises `B3CALL drv_boot_x`. The eighteen
   fixtures still fail their rules. `KGO_PAIR b3`'s `mov [bp+10], cs` is a
   registered B3 hand-down, marked on the invocation line - the only raw line
   an expansion's body has.
5. **Two crossings the plan did not count**: CFG_BOOT's `BLOBCALL bdk_apply`
   (the dock settings, under `DOCK_OPT`) is a `B2CALL`, +1; and `B3CALL
   cfx_cfgname` assembles as `push cs` + a near call inside `.boot3`, -1
   rather than the -2 predicted.
6. **The NOKZIP read-end guard** (section 13) was not in the tree; it is now,
   after `MODC_START` in kernel.asm. Both it and the `.boot3b` alignment
   guard were made to fire by hand, and NOKZIP, `NO_COLD_SLIDE`,
   `NO_HMA_DRV` and `DRVDIAG=1` assemble. **`BOOTMARK=1` through the
   Makefile assembles on all four kernels** (kern_big, kern_emu, kern_mega,
   kern_small; `kernel-full.bin` into private trees), and did at the
   starting commit `dafa327b` on kern_big and kern_emu as well. What makes it
   fit is NOT the `BOOTMARK` arm (Makefile, `ifneq ($(BOOTMARK),)`), which
   adds `-DBOOT_MARK` and nothing else: it is the generic `$(KNOBS)` arm
   beside `BOOT2_XSEC := 1`, which puts `-DKERN_KNOB` on any knob build but
   the exempt ones, and `KERN_KNOB` gives `DSK_OVLPAD` 1,024
   (kernel/dskwin.inc). So a raw `nasm -DBOOT_MARK` WITHOUT `-DKERN_KNOB`
   fails `.ovl`'s window guard (kernel.asm, "the boot overlay's window half
   has outgrown the FAT window plus dsk_secbuf") on kern_big AND kern_emu -
   identically at `dafa327b` (the wave-2 verifier's test) - and assembles on
   kern_mega and kern_small; with `-DKERN_KNOB` all four assemble. Neither
   half is this wave's. (The first draft of this item said only kern_big
   failed and credited "the Makefile's knob arm"; corrected after the
   wave-2 verifier, round 1.)
7. **`vmm_boot_x` (kern_emu) did not move**: the plan's "may join". It shares
   the blob's `bvs_cgo` with `xm_boot_x`'s shape and is wave 4's to take with
   the rest of the adapter bodies.
8. **`B3_RES` counts `.boot3b`'s pad** as well as the buffer, so that `B3_END`
   is the scratch's real end: 926 bytes on kern_big (414 + 512), 914 on
   kern_mega. `kernsize`'s `boot3` line prints it as "nobits".
9. **`tools/os88sym.py`** files `.boot3b` at `B3_RAM` (`ovc_buf` resolves to
   17DA:0C60, linear 0x18A00 on kern_big); `os88ladder --selfcheck` and
   `os88intguide --selfcheck` both pass.

**Host checks run**: `make` (fast tier 75 passed, 1 skipped), `make mega`,
`make emu`; `tools/kernsize.py` on kern_big, kern_small and kern_mega (each
build's own lines, before and after, above); `tests/unit/t_blobruns.py`,
`t_canary.py`, `t_abilock.py`; `tools/checkdocs.py`; `tools/os88index.py
--check`; the cheap soak host checks run by their scripts: `bootfloor`
(38 checks) and `bootfence` (17), `coldpic` (`os88romfix.py --check`),
`resident`; `os88rom.py kernel` into the scratchpad.

**Rows to name** for the single final pass. fast (run inside `make`):
`ovlchk`, `canary`, `blobruns`, `bootfence`, `romtool`, `image`,
`diskverify`, `livefull`. full: `bootsmoke`, `small128`, `megahigh`,
`sysfree`. soak, host-only: `bootfloor`, `coldpic`, `resident`. soak,
emulator - the boot and its loads: `kzboot`, `kzboot-off`, `bootstatus`
(the status line through `B2CALL`), `blobsum`, `bootfloor-ab`, `hdboot`,
`hdtakeboot`, `install`, `cylrun`, `splashbar`, `postboot`, `heapmap`,
`v20boot`, `pravetzboot`; the re-pointers: `coldslide` (its fourth group is
22 words now), `rombig`, `romnodisk`, `hmacold`, `hmadrv`, `hmahiber`,
`hmapool`, `xmhwalk`, `vgadrop`, `buildmatrix`; what `drv_boot_x` and the
reader apply: `fddpage` (`ovl_fdd_apply` and its door), `deskfdd`,
`vgacolor` (Force Color through `b3_cgo`), `extdmod` and `dispmode`
(`bxf_sync`), `desksc`, `stikboot`, `stikmiss` (`ovl_sc_load` and
`ovc_buf`), `hdauto` (a `DRVE_NOVOL` drop through `b3_cgo`), `usbmouse`,
`vesa`, `vesablitd` (the boot floor's 512-byte grid), `ethernet`,
`colorpresets`, `hibernatedrv`, `autostart`, `hibernate`, `fontpick`; kern_emu
`vmmouse`; kern_mega `megaovl`, `megamod`, `megahiber`, `megahma`,
`megarmbext`, `rdboot`.

### 16.8 Wave 3 as built (build, 2026-11-08)

Built against this tree's code; where the code disagreed with the plan the
code won. Host checks only (CLAUDE.md's Testing rule): no emulator ran, no
row was run but the fast tier inside `make` and the host scripts named
below.

**What moved** (kern_big, kern_emu, kern_mega): **the clock ladder whole**
(owner, third round, Q8: "move related things together") - `clk_init`,
`clk_probe`, `clk_commit`, the four rungs' probes and reads (`clk_at_*`,
`clk_yr_join`, `clk_xt_charge`, `clk_ns_*`, `clk_tobcd`, `clk_rp_*`,
`clk_bios_*`) and `clk_bcd`, 1,782 bytes, one block of `kernel/clock.inc`;
`sch_idle_start` (35); `vid_ctx_init` (63, `KERN_BIG` builds); `mem_init_x`
(78); and, from the blob, `wm_init` (34). All to `.boot3`.

**The callers, checked.** Every one of them has exactly one caller, a line of
`kmain_o` before `drv_boot_x` (MARKs 5, 6, 8, 11 and 14), so each runs while
`.boot3` is live - after `kz_all` and before `mem_unblob_x` - and before the
mount, which is the stronger deadline the `.ovl` homes had. No rung is
reached from outside `clk_init`'s cone: the only names `clock.inc`'s ladder
shares with the rest of the tree are `.bss` (`clk_tier`, `clk_rtc`, the
fields) and `clk_str`'s probe scratch (SPEC.md 37.90), and the ladder's own
far exit is `BLOBMARK`'s `call KERNEL_SEG:ovw_mark_stamp` (`BOOTMARK=1`
only). **The RTC's WRITE half is untouched**: `clockw.inc` (CTRL.DRV's
`.modc`) calls only its own labels and its own `clkw_*` expansions of the
six `CLK_*_BODY` helpers (SPEC.md 37.94), and names no ladder label - so a
Control Panel clock set after the drop runs exactly as before. `mem_init_x`'s
one COLD_SEG word (`wm_cbd`) left `.ovl`'s group for `.boot3`'s: `os88cfx`
counts 93 + 2 + 3 + 23 (was 93 + 3 + 3 + 22), 197 of 256 bytes, and the ROM
lists conserve (`os88rom.py kernel`: 93+2+3+23, 266 bytes of the window
spare, unchanged). It runs at MARK 11, after the slide (MARK 10) and after
`rom_patch`, so both re-pointers have reached it.

**What was measured** (the shipped KZIP maps and kernel.kz.json, against the
starting commit `a6a351a3` built in a worktree at the same commit count):

| kernel | resident | `.boot2` (map, KZIP) | `.ovl` (of 5,120) | `.boot3` (+ `.boot3b`) | lift (para) | guard 5 headroom | boot arena | KERNEL.SYS |
|---|---:|---|---|---|---:|---:|---:|---|
| kern_big | 96,768, +0 | 4,489 -> **4,456** (-33) of 6,656 | 5,119 -> **3,161** (1,959 free) | 2,754 -> **4,746** (+ 982) at 0x17DA | 224 -> **352** | 19,968 -> 17,920 (128KB) | 22,528 -> 20,480 | 87,200 -> 87,248 (171 sectors, +0; stage 2 158, +0) |
| kern_emu | 96,768, +0 | 4,605 -> **4,572** (-33) of 7,168 | 5,119 -> **3,161** | 2,755 -> **4,747** (+ 981) | 224 -> **352** | 19,456 -> 17,408 | 22,016 -> 19,968 | 87,872 -> 87,904 (172, +0) |
| kern_mega | 62,976 (MEGA_RESIDENT), +0 | 4,281 -> **4,248** (-33) of 7,168 | 5,118 -> **3,160** (1,960 free) | 2,670 -> **4,662** (+ 970) at 0x2140 | 256 -> **384** | 505,344 -> 503,296 (640KB) | 507,904 -> 505,856 | 118,400 -> 118,448 (232, +0; stage 2 218, +0) |
| kern_small | 58,880, +0 | 3,463, identical | 1,474, identical | 1,441, identical | 160 | 59,904 | 62,464 | 55,408, identical |

`kernsize`'s NOKZIP `.boot2` (section 16.6's note) reads 4,295 -> 4,262 on
kern_big and 4,087 -> 4,054 on kern_mega. KERNEL.SYS grows 32-48 bytes, not
by what moved: the bytes were in the packed stream already, and the zeros
they leave in `.ovl` pack to almost nothing. R grows instead (kern_big
18,944 -> 20,480), and with `.boot3`'s end the lift is the read's term on
all three kernels: four steps, 2,048 bytes of boot RAM, 0 resident. The
blob stays at 0x19600..0x1B000 on kern_big and 0x22C00..0x24800 on kern_mega,
across no 64KB page. `blobruns`: every geometry's stage-1 and stage-2 call
counts unchanged (2/2/2/2/3/3 and 11/11/9/8/8/8).

**What was proven byte-identical, at one commit** (`make`, `make mega` and
`make emu` in a worktree of `a6a351a3`, against this tree before
committing): **kern_small's `kernel-full.bin` section by section,
`kernel.bin`, `kernel.sys`, `boot.bin`, `boot360.bin` and
`small360.img`/`small720.img`**, and its `kernsize` lines. On kern_big,
kern_emu and kern_mega every module section (`.modc` .. `.modfs`), `.vga`
and `.mega` are identical; `.text` differs in 3 bytes and `.cold` in 1, each
an immediate naming a blob offset that moved under it (`sch_isr`'s and
`dkb_geom`'s SPLCALL targets, `kbld_blob`, `dsk_xfer`'s notch: +1 behind
`kmain_o`'s longer call, -33 behind `wm_init`'s room); `.modmap` moves with
MODC_START. Of the shipped images every apps, category, media, WIN16 and
ARCADE disk is identical; the four system disks, `emu.img`,
`mega.img`/`mega120.img` differ (KERNEL.SYS and its neighbours), and
`util360.img` only by `SYSCHECK.O88` (section 16.4's note).

**Deviations.**

1. **The clock ladder moved WHOLE**, the plan's option, by the owner's Q8
   answer, so `.ovl` frees 1,958 bytes on kern_big (1,959 free), not 176
   (177): the wants it was for (BR-18, BR-88, BR-141, BR-157, BR-158, BR-25,
   gfce-6 / WP8's install, about +93..+103 with WP7's -45) fit with about
   1,850 to spare, on kern_big and kern_mega. The price is the lift: 224 ->
   352 (kern_mega 256 -> 384), which the wave-3 ledger rows (section 9,
   "~224", "~256") did not carry, because the 176 fitted under them. Wave 4's
   lift will be higher than section 9 predicts by the same steps (about 416
   paragraphs on kern_big, a 128KB arena about 23,000 rather than 24,064).
2. **kern_small is kept byte-identical by a build choice, not by having
   nothing to move.** The plan's "kern_small has no wave 3" missed that two
   of the bodies are shared source whose kern_small arm is `.ovl`:
   `sch_idle_start` (sched.inc) and, with Q8, the whole clock ladder
   (clock.inc; 174 bytes there, the rungs being `OS88_RTC`'s, SPEC.md
   37.0.1). Each is written `%ifndef KERN_SMALL` / `B3SECT` / `%else` /
   `section .ovl`, the kern_small arm LAST (SPEC.md 2.5.3.2's rule), and
   called by **OVSCALL**, OVBCALL's mirror - whose non-small arm was
   `BLOBCALL` (for `wm_init` alone) and is `B3CALL` now. So one macro means
   "`.ovl` on kern_small, `.boot3` elsewhere", and that is why **`wm_init`
   moved too** (a wave-4 body, pulled forward: `.boot2` -33, `.boot3` +34,
   +1 at the call). The "one ladder" argument of section 5.1 would move them
   on kern_small as well; the brief said kern_small unchanged, and its
   `.ovl` has no want named, so it was not done.
3. **`mem_init_x` leaves OVBCALL**: its body is `B3SECT` on every kernel
   (kern_small's arm was `.boot3` since wave 1), and `kmain_o` calls it with
   a plain `B3CALL`. OVBCALL keeps five bodies.
4. **`clock.inc` was restructured to hold the ladder in ONE conditional
   block**: its two empty `section .text` / `section .ovl` interludes (a
   comment run and the `CLK_NS_*` equates) went, and `clk_bcd`'s block moved
   textually from after the bar's formatter to the ladder's end, which is
   where it already sat in `.ovl` on every kernel - so kern_small's `.ovl`
   and every `.text` byte are unchanged (the cmp above).
5. **`os88ovlchk` gained an arm check, not a relaxation.** Its expander
   keeps both of OVSCALL's arms, so the `B3CALL` arm failed B8 against the
   one model (kern_small's `.ovl`). B8 now judges OVSCALL's `B3CALL` by the
   section its target's NON-small arm places it in (`_walk_alt`: the section
   a `%if(n)def KERN_SMALL` block's other arm switched to), and refuses one
   that is not a `B3SECT` - which closes, for OVSCALL, the hole OVBCALL still
   has (an OVBCALL at a body plain in `.boot3` passes rule 2d and calls
   `FAT_SEG:` with a `.boot3` offset on kern_big; not this wave's). A
   positive control (the shape) and a nineteenth fixture (OVSCALL at a body
   plain in `.ovl`) were added; broken on purpose in the real tree
   (`clock.inc`'s B3SECT arm written `section .ovl`), B8 went red at
   `OVSCALL clk_init`, and green restored. Rule 2e's window-call patterns
   (`OVLCALL`, `WINREF`) include OVSCALL, which IS a window call on the
   model's build.
6. **`tests/ovlrefs.txt` is unchanged**: the `clk_init`, `sch_idle_start`
   and `wm_init` rows are still the model's (`.ovl` on kern_small), and the
   `vid_ctx_init` row, now a `.boot3` label reached from the blob, needs no
   row under B1 and is left to rule 2c's stale-row rule, which leaves rows
   naming `.boot3` labels alone.

**Host checks run**: `make` (fast tier 75 passed, 1 skipped), `make mega`,
`make emu`; `tools/kernsize.py` on kern_big, kern_small, kern_mega and
kern_emu (before and after, above; kern_small's lines identical);
`tests/unit/t_blobruns.py`, `t_canary.py`, `t_abilock.py`;
`tools/checkdocs.py`; `tools/os88index.py --check`; the cheap soak host
checks by their scripts: `bootfloor` (38 checks), `bootfence` (17),
`coldpic` (`os88romfix.py --check`), `resident`; `os88rom.py kernel` for
both ROM kernels and `--selfcheck`; `os88ladder --selfcheck`, `os88intguide
--selfcheck`.

**Rows to name** for the single final pass. fast (inside `make`): `ovlchk`,
`canary`, `blobruns`, `bootfence`, `romtool`, `image`, `diskverify`,
`livefull`. full: `bootsmoke`, `small128`, `megahigh`. soak, host-only:
`bootfloor`, `coldpic`, `resident`. soak, emulator - the clock and its write
half after the drop: `dtwrite` (QEMU's rung 1, set, reboot, read the bar),
`deskclock`, `fontpick`; the idle task: `schacct`, `uiblock`; display 0's
context: `dispapp`, `dualcheck`, `dispmode`, `vgadrop`; the heap's floor and
the re-pointers of `mem_init_x`'s word: `heapmap`, `bootfloor-ab`,
`coldslide`, `rombig`, `romsmall`, `romnodisk`, `hmacold`, `hmadrv`,
`hmahiber`, `buildmatrix`; the lift and its loads: `kzboot`, `kzboot-off`,
`splashbar`, `bootstatus`, `blobsum`, `hdboot`, `hdtakeboot`, `install`,
`cylrun`, `postboot`, `v20boot`, `pravetzboot`; kern_mega `megaovl`,
`megamod`, `megahiber`; kern_emu `vmmouse`.

### 16.9 boot-4 and boot-10 as built (build, 2026-11-08)

Wave 4's two preconditions, and nothing of wave 4 itself. Host checks only
(CLAUDE.md's Testing rule): no emulator ran and no soak row was run through
the suite; `resident`, `bootfloor` and `coldpic`, host-only, were run by
their scripts as this wave's checks.

**The audit came first, and it is a gate now.** boot-4 asked for every
first-tick store to be audited for an address in sectors 5-8 before
SPL_RESIDENT was lowered. A by-hand audit is an argument; so
`tests/unit/t_resident.py` gained a second check that walks the ticks' code
(from `spl_tick` and `spl_isr`, through near calls, `BLOBCALL`, `spw_near`'s
BP target and fall-through, over a `-Le` listing so macro expansions are what
they assemble to) on every built kernel and collects every DS memory operand
and every `mov si/di/bx, label` naming a kernel label. One in `.text` at or
past SPL_RESIDENT * 512, or in `.bss`/`.lowbss`, fails unless
`tests/tickrefs.txt` explains it. On kern_big it walks 42 procs and 161
references (153 before the verifier's round 1, below), and finds **seven** past the 5-sector window: `vx_ep` and `vx_bpp`
(0x0C57, 0x0C5B - READS, behind the VESA kind, which no first tick has) and
`cur_shchk`, `cur_disp`, `mouse_x`, `vid_rowtab` and `ui_post` (stores behind
`bvs_apply`'s stack test, `jne .homed`, which the first tick takes). All seven
are registered with their reason; kern_small has four of them, kern_mega and
kern_emu all seven. **Two of the seven were not in the plan's list**:
`vx_ep`/`vx_bpp` sit in sector 6 today, so lowering the head without a gate
would have left vid_setmode reading unloaded memory on any arm that could
reach the VESA kind early; `vid_rowtab` is a `.lowbss` store through
`[ss:si]`. The one first-tick store that IS made and must survive,
`[vid_cgamode]` (0x09E9 on kern_big - 23 bytes inside the window), is
past `spw_resident_end`, so it gets its own `%if` at kernel.asm's foot.
**Broken on purpose**: the registry less `mouse_x` is red on all four kernels;
a line nothing needs is red; `inc byte [evq_head]` planted in spl_tick in
place of `inc byte [spl_busy]` is red on every kernel; both foot `%if`s were
made to fire by hand.

**Verifier round 1 (build, 2026-11-08): `cs:` is DS in `.text`.** The first
draft skipped every `[cs:...]` operand, which is right in `.boot2`/`.boot3`
(CS is the blob's segment there) and wrong in the `.text` procs behind
`spw_near`'s far door, where CS = KERNEL_SEG = DS - and those are what the
tick code reads most: `gfx_rowbase`/`gfx_rowbase_calc` read `[cs:vid_rowmax]`,
`[cs:vid_bmask]`, `[cs:vid_bshift]` and `[cs:vid_stride]` on every kernel,
and kern_mega's `vid_setmode`/`vid_cga_equip` read `[cs:k_vsa]`, `[cs:k_vsc]`
and `[cs:k_seg0]`. The walk now exempts `cs:` only in a proc whose own
section is `.boot2` or `.boot3`. Every one of those words is inside the window
today (0x755-0x834), so the reference counts rose (kern_big 153 -> 161,
kern_small 142 -> 150, kern_mega 152 -> 164, kern_emu 153 -> 161) and nothing
new failed: `tests/tickrefs.txt` is unchanged. **Broken on purpose**:
`cmp ax, [cs:gfx_ln_ink]` (`.bss`) and `cmp ax, [cs:wm_paint_all]` (`.text`
far past the window) planted at the head of `gfx_rowbase` are red on all four
kernels; the source was restored.

**What moved (boot-4).**

- `spw_resident_end` moved below `[spl_live]`, `[blk_on]`, `[spl_busy]` and
  `[spl_quiet]` (0 bytes; kern_big 0x922 -> 0x926). It is `spw_resident_end
  equ $`, not a `label:`: tools/os88rom.py finds `.text` data words off the
  source, and a bare label followed by `vid_text:` reads as a marker - written
  as a label it dropped vid_text's first 60 bytes of code out of the HMA
  image's hash and changed COLDHMA.SYS. Found by the one-commit `cmp`, fixed
  before commit.
- `SPL_RESIDENT equ (SPL_RES_SIZE + 511) / 512` at kernel.asm's foot;
  splash.inc types nothing. **5 on kern_big, kern_small and kern_emu; 6 on
  kern_mega** (its window ends at 0xA13 - the plan's "9 to 5" was kern_big's
  and kern_small's).
- The plain head is `max(SPL_RESIDENT, KZ_HEADMIN)`, read off pass 1's map
  by `tools/os88ksig.py head`; pass 1's `KZ_HEADSEC` is a 0 placeholder like
  the other KZ numbers (os88rom, os88romfix, os88modcost and t_khpknobs say
  0 now too), and the foot refuses a pass 2 whose head is shorter than the
  window.

**Deviation 1: kern_big's head stays 9.** The plan (and phase1.md WP6, and
areas/boot.md's "the bar's start does not move on any geometry") priced the
lower head as disk bytes only. t_blobruns' model, at this build's packed
length, says otherwise:

| blob, head | 360KB | 720KB | 1.2MB | 1.44MB |
|---|---:|---:|---:|---:|
| 13, 9 (shipped) | 11 | 11 | 9 | 8 |
| 13, 5 | **12** | **12** | 9 | **7** |
| 7, 9 (wave 4, head kept) | 12 | 12 | 8 | 7 |
| 7, 5 or 6 (wave 4) | 11 | 11 | 8 | 7 |

(stage-2 int 13h calls, PREDICTED.) A 9-sector head ends exactly on a
cylinder on the two 9-sector disks; 5 restarts the tail four sectors short of
one. So today the lower head buys ~400 bytes of KERNEL.SYS and 1.44MB's call
(its head becomes one run, before the bar) for a call after the bar on the
owner's 5150 geometry - a trade the STAGE2 ratchet would have refused at
`make`, and the owner's rather than this wave's. `KZ_HEADMIN` is 9 on
kern_big, kern_emu, kern_mega and every knob, 0 on kern_small, and the
Makefile carries the table. **Wave 4 should set kern_big's to 0 (or 6) with
its blob**: at 7 sectors the window's head is the best on every disk.
**Owner question**: take kern_big's 5-sector head now (360/720 +1 call after
the bar, 1.44MB -1 before it, -400 bytes), or with wave 4 (the plan's
order, no call lost anywhere)? This build assumes the latter.

**What moved (boot-10).** `tools/os88ksig.py` chooses KSIG_OFF per arm
between the two KZIP passes (NOKZIP: before its one pass, model only, then a
`verify` on the file) and writes `$(BUILD)/ksig.inc`, which boot2.asm
includes and KSIGDEF2 reads; `verify` re-walks the final KERNEL.SYS and
records `ksig_off` in kernel.kz.json. The Makefile's `KSIGGEOM` names each
arm's shipped system disks as their os88disk.py lines build them; the -512
and +1024 arms are gone. It picks the lowest sector in the tail, inside the
handoff's 64KB and the read, live on every disk, distinct from both
neighbours, and fails the build otherwise (made to: a 40-sector tail names
1.44MB as the disk with no live sector). It asserts on every build that the
head's run bound (BR-1 part 2, the owner's open question) moves no answer -
the tail starts where the head ends - and part 2 is untouched. `t_canary`
reads each kernel's assembled `KSIG_OFF` out of its own map, checks the image
carries that tree's KERNEL.SYS, and walks mega.img, mega120.img and emu.img in
full when current (named as stale otherwise). **Broken on purpose**: with
`live()` answering True the generator chose file sector 22 and `t_canary`
went red on all four kern_big disks.

**Measured, at one commit** (the starting commit in a worktree under the
scratchpad, `make`, `make mega`, `make emu`, against this tree before
committing):

| kernel | resident | `.boot2` (map) | `.ovl` | `.boot3` | KERNEL.SYS | SPL_RESIDENT | head | canary |
|---|---:|---:|---:|---:|---|---:|---:|---|
| kern_big | 96,768, +0 | 4,456, +0 | 3,161 | 4,746 | 87,248 (171 sectors), +0 | 5 | 9 | file sector 57 (22,528), unchanged |
| kern_emu | +0 | 4,572, +0 | 3,161 | 4,747 | 87,904 (172), +0 | 5 | 9 | 57 (22,016), unchanged |
| kern_small | 58,880, +0 | 3,463, +0 (7 immediates) | 1,474 | 1,441 | 55,408 -> **55,136** (109 -> **108**) | 5 | **5** | **33** (11,264), was 57 |
| kern_mega | 62,976 (MEGA_RESIDENT), +0 | 4,248, +0 | 3,160 | 4,662 | 118,448 (232), +0 | 6 | 9 | 57 (22,016), unchanged |

Every `kernsize` line is unchanged on all three budgeted kernels.
**Byte-identical**: kernel.bin, kernel-full.bin, kernel.sys, boot.bin,
boot360.bin, boot120.bin and boothd.bin on kern_big, kern_emu and kern_mega,
and coldhma.sys; of all 34 images the starting commit built, every one but
small360.img and small720.img - so this wave changes no shipped disk of
kern_big, no apps/category/media/WIN16/ARCADE disk, and neither kern_mega disk
nor emu.img. kern_small's kernel.bin differs in 7 bytes, all in the blob
(the head and canary immediates); its packed tail grew by the four sectors the
head gave it, and KERNEL.SYS lost a sector, so its two disks move behind it.
`t_blobruns` is unchanged on every disk (small360/small720 8 either way).

**Rows to name** for the single final pass: `canary`, `blobruns`, `image`,
`diskverify` (fast, inside `make`); `resident` (soak, its `secs` re-measured
at 9.5 for four kernels), `bootfloor`, `coldpic`, `buildmatrix` (the NOKZIP
and knob arms of the generator; NOKZIP was built here into a private tree and
chose 22,016), `kzboot`, `kzboot-off`, `smallboot`, `romsmall`, `splashbar`,
`splashspin`, `cylrun`, `fddpage`, `megacz`, `pravetzboot` (soak); `small128`,
`bootsmoke` (full).

**Left undone, on purpose**: kern_big's lower head (deviation 1, the owner's
or wave 4's); kern_small's on-demand small.img is still outside both the
generator's and t_canary's set (areas/boot.md question 4).

### 16.10 Wave 4 as built (build, 2026-11-08)

Built against this tree's code; where the code disagreed with the plan the
code won. Host checks only (CLAUDE.md's Testing rule): no emulator ran, no
soak row was run through the suite, and nothing was booted; `bootfloor`,
`bootfence`, `coldpic` and `resident` ran by their scripts.

**What moved, on every kernel that has it, all under `B3SECT`:**
`kmain_o` with `kmo_mark` (kernel.asm); the boot's adapter switch - KSW_SECT's
`bvs_avail_test` .. `bdk_reflow` and `bvw_rowcalc` (driver.inc), hopping
through `.boot3`'s `b3_kgo`/`b3_cgo` (`BSW_GO`/`BSW_CGO`), so `KGO_PAIR bvs`
and the wave-2 door `bvs_disp_far` went; `xm_boot_x` (xmem.inc); `vx_boot_x`
and `vx_blk` (vidext.inc; `VXV_INIT` copies the block, so `mov dx, cs` is a
registered B3 hand-down); kern_emu's `vmm_boot_x` (vmmouse.inc, wave 2's
deferral); `ovl_dock_geom`/`ovd_geom` (dock.inc, BDKGO now `b3_kgo`); and on
kern_small its `DESK_ROWCALC` copy with its `bvw_` pair (desk.inc) and its
one-byte `drv_boot_x` rung (driver.inc). **Stayed in the blob**: the loader,
the splash, the first tick's probe (`vid_detect`, `bvs_apply`/`bvs_vtab`,
`vid_init`), the status line (`ovl_spl_msg_*`, `spl_m*`, `bdt_*`), and two
doors: `bvs_apply_f` (new, 4 bytes: `.boot3`'s VSW_SWITCH and VSW_DISP reach
the probe's `VID_APPLY` through it) and wave 2's `ovl_fdd_cylok`.
`kmain`'s call is `call B3_SEG:kmain_o` and `dkb_geom`'s `call
B3_SEG:ovl_dock_geom` (behind its own `[spl_fseg]` test); `SPLGATE`,
`SPLGATE1`, `SPLSTUB` and `spl_gate` are deleted.

**What was measured** (the shipped KZIP maps and kernel.kz.json against the
starting commit `56052c57` built in a worktree at the same commit count;
`kernsize`'s `boot2` is the NOKZIP arm, section 16.6's note):

| kernel | resident | `.boot2` (map, KZIP) | `BOOT2_SECS` | `.ovl` | `.boot3` (+ `.boot3b`) | lift (para) | guard 5 headroom | boot arena | KERNEL.SYS | head | canary |
|---|---|---|---:|---:|---|---:|---:|---:|---|---:|---|
| kern_big | 96,768, +0 (`.text` 40,340 -> 40,318, **-22**) | 4,456 -> **3,296** (288 free) | 13 -> **7** | 3,161 | 4,746 -> **5,847** (+ 905) | 352 -> **416** | 17,920 -> **19,968** (128KB) | 20,480 -> **22,528** | 87,248 -> **84,704** (171 -> 166 sectors) | 9 -> **5** | file sector 57 -> **21** (KSIG_OFF 7,168; 2 qualify) |
| kern_emu | 96,768, +0 (`.text` -22) | 4,572 -> **3,311** (273 free) | 14 -> **7** | 3,161 | 4,747 -> **5,949** (+ 803) | 352 -> **416** | 17,408 -> 19,968 | 19,968 -> **22,528** | 87,904 -> **84,912** (172 -> 166) | 9 -> **5** | 57 -> **21** (37 qualify) |
| kern_mega | MEGA_RESIDENT 62,976, +0 (`.text` 46,086 -> 46,064, **-22**) | 4,248 -> **3,213** (371 free) | 14 -> **7** | 3,160 | 4,662 -> **5,637** (+ 1,019) | 384 -> **448** | 503,296 -> **505,856** (640KB) | 505,856 -> **508,416** | 118,448 -> **115,360** (232 -> 226) | 9 -> **6** | 57 -> **21** (14 qualify) |
| kern_small | 58,880, +0 (`.text` 30,046 -> 30,028, **-18**) | 3,463 -> **3,102** (482 free) | 11 -> **7** | 1,474 | 1,441 -> **1,803** | 160 -> **320** (page term) | 59,904 -> **59,392** | 62,464 -> **61,952** | 55,136 -> **53,424** (108 -> 105) | 5 | 33 -> **15** (KSIG_OFF 4,096; 45 qualify) |

The section-9 ledger predicted kern_big's residue at ~3,330 (MEASURED 3,296),
-8 resident (MEASURED -22, deviation 1), lift ~320 (MEASURED 416, the 128
paragraphs section 16.8's deviation 1 foresaw) and the arena back at ~23,000
(MEASURED 22,528). KERNEL.SYS shrinks because the blob is the file's one
unpacked part.

**`int 13h` calls, before the bar / in total** (stage 1 + the head / stage 1
+ stage 2; `t_blobruns`' model over every shipped system disk, PREDICTED
until `os88boot` measures them; kern_mega also under a 286's track bound):

| disk | before (blob 13/14, head 9; small 11, 5) | after (blob 7, the window's head) |
|---|---|---|
| os8088-360, os8088-720 | 3 / 13 | **3 / 13** |
| os8088-120 | 4 / 11 | **3 / 9** |
| os8088 (1.44MB) | 4 / 10 | **3 / 9** |
| small360, small720 | 5 / 11 | **3 / 9** |
| emu.img | 4 / 10 | **3 / 9** |
| mega.img, mega120.img (cylinder model) | 4 / 13, 4 / 14 | **3 / 12, 3 / 13** |
| mega.img, mega120.img (track-bounded) | 4 / 18, 4 / 21 | **3 / 18, 3 / 19** |

Sectors read before the bar: kern_big 22 -> **12**, kern_emu 23 -> 12,
kern_mega 23 -> 13, kern_small 16 -> 12. `t_blobruns`' ratchet: stage 1
2/2/2/2/3/3 -> **2/2/2/2/2/2**, stage 2 11/11/9/8/8/8 -> **11/11/7/7/7/7**;
the IMAGES and STAGE2 rows moved with their reasons.

**The head (the brief's choice).** Walked per head with the model above,
`os88kz.py` re-run at each head for its exact `ksecs`: on kern_big heads 5 and
6 give 3/13, 3/13, 3/9, 3/9; 7 gives 4/14 on 360KB and 720KB; 8 and 9 give
4/15 there. kern_small 5-8 tie at 3/9, 9 is 4/10; kern_emu and kern_mega tie
from the window up (5 or 6 to 9). So **`KZ_HEADMIN` is 0 on every kernel** -
the head is the window, 5 sectors (6 on kern_mega) - minimising calls before
the bar first and in total second on every disk, and the shorter file on
the ties. BR-1 part 2 (the head at the track bound) is untouched; the head is
one track on every shipped disk now, so it costs no call anywhere.

**What was proven byte-identical, at one commit** (`make`, `make mega` and
`make emu` in a worktree of `56052c57` under the scratchpad, against this
tree before committing, `cmp`): every apps, category, media, WIN16 and
ARCADE disk in all its geometries (`apps*.img`, `more720.img`,
`office360`/`network360`/`games360`/`emu360.img`, `media360.img`,
`win16*.img`, `arcrel*.img`) and kern_mega's own apps, WIN16 and XTMP3 disks
(`megaapps*.img`, `megawin16*.img`, `megaxtmp3*.img`). `util360.img` differs
in `SYSCHECK.O88` (section 16.4's note: it is kern_big's system disk's
manifest) **and in `FSCHECK.DRV`**, every other entry's size and cluster the
same - see the correction below. Every kernel, its boot sectors,
`boothd.bin`, `COLDHMA.SYS`, every kernel module and the nine system disks
differ, as this wave means them to: no kernel was claimed byte-identical.

*Correction (build, 2026-11-08, verifier round 1).* The paragraph above, and
commit `8695fa9e`'s message, first said `util360.img` differs "only in
`SYSCHECK.O88`" and named `COLDHMA.SYS, CTRL.DRV` as the modules that move.
Both were wrong, and the one-commit `cmp` that settles it (worktrees of
`56052c57` and `8695fa9e`, both `make` and `make mega` with
`OS88_BUILDNUM=969`) says: `util360.img` differs in sector 0 (the BPB volume
serial, bytes 39-42, and nothing else), in `SYSCHECK.O88` (sectors 25, 27, 45
and 46; 14,713 bytes in both at that build number) and in `FSCHECK.DRV`
(sectors 162-172; 5,278 bytes in both, 60 of them differ). `FSCHECK.DRV` is
a kern_big module that `os88mod -m 10=` cuts out of the kernel image, so it
moves whenever the kernel does - expected, and missed by the record. And
EVERY kernel module differs, not two: kern_big's `CTRL`, `DESK`, `DISKOPS`,
`DOCK`, `EXTD`, `FORMAT`, `FSCHECK`, `HIBER`, `SERIAL`, `STIKMENU` and
`WIN95.DRV`, with `KERNEL.SYS`, `BOOTHD.SYS` and `COLDHMA.SYS`; kern_small's
`CTRL`, `DISKOPS`, `FDLG`, `FILECP`, `FORMAT` and `SERIAL.DRV`, with its
`KERNEL.SYS`; kern_mega's thirteen (the same eleven modules as kern_big's,
with its `KERNEL.SYS` and `BOOTHD.SYS`). Every non-kernel driver (`CDROM`,
`EMS`, `ETHER`, `HDD`, `HDDTOOL`, `HTTPFS`, `HTTPPAGE`, `NET`, `PCIETHER`,
`RAMDISK`, `RAMPAGE`, `SAVER`, `SOUND`, `THMPAL`, `USBMOUSE`, `VESA`, `XMEM`)
is byte-identical. The round-1 fix commit is comments only (the six stale
"kmain_o is in the blob" comments the verifier named, and the same claim in
desk.inc, hiber.inc, the KGO_PAIR header, VSW_SWITCH's and VID_CTXCAP's
headers, kmain_o's mod_init loop and os88ovlchk's TEXP note), and its own
one-commit `cmp` (a worktree of `8695fa9e` against the fixed tree, `make` and
`make mega` at `OS88_BUILDNUM=969`) found every built
`.img`/`.bin`/`.sys`/`.drv`/`.o88`/`.rom` file under `build/` identical - 406
of 408 by name, the other two WIN16 build caches whose names carry a
path-keyed hash, identical in content under their own names - and every
`kernsize` line the same on all three kernels.

**Deviations.**

1. **`spl_gate` is deleted, not kept**: with `kmain`'s and `dkb_geom`'s
   `SPLGATE1` sites gone (the plan's -4 each), nothing called the gate or its
   macros, so `SPLGATE`, `SPLGATE1`, `SPLSTUB` and the 14-byte `spl_gate`
   went too - -22 resident on kern_big, kern_emu and kern_mega and -18 on
   kern_small (no `dkb_geom`), against the plan's -8.
2. **`bvs_apply`'s door is reached through `%ifidn %1, bvs` arms** in
   VSW_SWITCH and VSW_DISP (`B2CALL bvs_apply_f`), the existing `cfx_ctxcap`
   precedent; the panel's `vid` instance assembles as before. That needed
   `tools/os88ovlchk.py`'s main near-call expander to decide `%ifidn` on a
   macro's known arguments as rules B1-B9's expander already did (both arms
   kept, the `vid` arm's `call bvs_apply` read as a `.boot3` -> `.boot2` near
   call), and VSW_SWITCH's kern_small `KSWN wm_su_drop_all` arm to be wrapped
   in `%ifnidn %1, bvs` (B4 keeps both arms of a `%ifdef`; the `bvs` instance
   is kern_big's alone, so the bytes are unchanged on every kernel).
3. **kern_small's `drv_boot_x` rung moved too** (the plan's list did not name
   it): one `B3CALL drv_boot_x` in `kmain_o` on every kernel, and wave 2's
   `%ifdef OS88_DRIVERS` arm at the call went.
4. **`vmm_boot_x` moved** (kern_emu), as wave 2's deviation 7 deferred; its
   `bvs_cgo` hops are `b3_cgo`.
5. **The page term is built, and generically**: the plan made it kern_small's
   and conditional on `os88boot` confirming the third stage-1 call. The call
   is boot.asm's own code (its second run bound, at the destination's DMA
   page), and `t_blobruns` walks that code: at the read's lift kern_small's
   7-sector blob would sit at 0xF800 and read 4 + 2 + 1. So `BLOB_LIFT`
   rounds the blob onto the page whenever it would straddle one, on every
   kernel - it binds on kern_small alone (lift 192 -> **320**, the blob at
   0x10000, read 6 + 1; 512 bytes of its 128KB arena) and is 0 elsewhere -
   and a foot guard (5a) refuses a straddling blob. The call count stays
   PREDICTED until `os88boot` on small360 measures it in the final pass.
6. **Every kernel is 7**: kern_emu and kern_mega lose their extra sector
   (`BOOT2_KNOBSEC` is `KERN_KNOB`'s alone now) and `BOOT2_SMALLSEC` is
   deleted; a knob build is 8 (`BOOTMARK=1` on kern_big and kern_small,
   `NOKZIP=1` and `NO_COLD_SLIDE` assembled in private trees).
7. **R19 did not cost a call, but it left no slack**: on 360KB and 720KB
   kern_big's packed tail now ends on the cylinder at LBA 180, so stage 2 is
   11 calls with **0** packed sectors to spare (`--tail 1` is 12, red);
   1.2MB has 15, 1.44MB 17, the small disks 5. A head of 8 or 9 buys 17
   sectors of room for two calls more, so it was not taken. **Wave 5's
   ~0.25 packed sectors on kern_big will cost those two disks a call after
   the bar unless the tail is a sector shorter by then**: the ratchet will say
   so at `make`, and that is the owner's trade.
8. **The canary moved by itself** (boot-10's generator): file sector 21 on
   kern_big, kern_emu and kern_mega, 15 on kern_small. kern_big's band has two
   live sectors now, where sector 57 had none to spare; `t_canary` walks all
   nine system images.
9. **R8 needed no `MARKW` arm**: `kmo_mark` moved with `kmain_o`, so MARKW's
   near call stays inside one section.
10. **The ratchet is `tests/unit/t_blobbodies.py`, a new FAST row
    (`blobbodies`, ~0.04 s)** over each built kernel's map (kern_mega's and
    kern_emu's trees when present and newer than the kernel's sources): every
    `.boot2` label filed in `tests/blobbodies.txt` as loader, splash, probe or
    status, or as a residue row with its reason, residue held to
    `RESIDUE_MAX` (2 on kern_big, kern_emu and kern_mega - the two doors - 0
    on kern_small), which only goes down. Broken on purpose three ways - a new
    `.boot2` label, a residue row a kernel lacks, `RESIDUE_MAX` one lower -
    and red each time.
11. **`tools/os88boot.py`** places `kmain_o`'s call sites at `B3_RAM`, not
    `BLOB_SEG_AT` (it would have armed every kmain_o breakpoint in the wrong
    RAM); `callsites()` was run and returns 41 sites starting at `B3_RAM`.
    `tests/ovlrefs.txt` lost the `xm_boot_x`, `bvs_switch` and `bvs_disp_far`
    rows and gained `vid_init`, `ovl_spl_msg_mouse`, `ovl_spl_msg_fdd` and
    `bvs_apply_f`.
12. **The installer needs no change**: an Install takes `BOOTHD.SYS` off the
    new system disk (SPEC.md 52.10.16), which carries this wave's
    `BOOT2_SECS`, `BLOB_SEG`, `SPL_FSEG`, `KZ_RPARA` and `KZ_HD`; SPEC.md
    52.10.16 now says that a kernel copied to C: without its sector will not
    boot, as every lift step already implied (R11).

**Host checks run**: `make` (fast tier 76 passed, 1 skipped), `make mega`,
`make emu`; `tools/kernsize.py` on kern_big, kern_small and kern_mega (before
and after, above); `tests/unit/t_blobruns.py` (and `--tail 1`, `--sectors 8`
red, as they should be), `t_canary.py`, `t_abilock.py`, `t_blobbodies.py`;
`tools/checkdocs.py`; `tools/os88index.py --check`; the cheap soak host checks
by their scripts: `bootfloor` (38 checks), `bootfence` (17), `coldpic`
(`os88romfix.py --check`), `resident` (4 kernels); `os88rom.py kernel` for
both ROM kernels (kern_big 93+2+0+26 sites, 266 bytes of the window spare;
kern_small 47+2+0+3 - the blob's group is empty, the totals conserved) and
`--selfcheck`; `os88ladder --selfcheck`, `os88intguide --selfcheck`;
`BOOTMARK=1` (kern_big and kern_small), `NOKZIP=1` and `NO_COLD_SLIDE`
(with and without `KERN_KNOB`) assembled into private trees.

**Rows to name** for the single final pass. fast (inside `make`): `ovlchk`,
`canary`, `blobruns`, **`blobbodies`** (new), `bootfence`, `romtool`,
`image`, `diskverify`, `livefull`. full: `bootsmoke`, `small128` (the page
term's 128KB machine), `megahigh`. soak, host-only: `bootfloor`, `coldpic`,
`resident`. soak, emulator - the shorter blob, the head and the canary:
`kzboot`, `kzboot-off`, `splashbar`, `splashspin`, `bootstatus`, `blobsum`,
`cylrun`, `fddpage`, `smallboot` (stage 1's two calls at 0x10000),
`pravetzboot`, `v20boot`, `megacz`; the load paths: `hdboot`, `hdtakeboot`,
`install`, `bootfloor-ab`, `postboot`, `heapmap`; `kmain_o` and the switch in
`.boot3`: `coldslide` (its fourth group is 26 words, the blob's 0),
`rombig`, `romsmall`, `romnodisk`, `hmacold`, `hmadrv`, `hmahiber`,
`vgadrop`, `dispmode`, `dualcheck`, `vgacolor`, `deskfdd`, `extdmod`,
`buildmatrix` (the knob arms at 8 sectors); VESA and the Dock: `vesa`,
`vesablitd`; the drivers `drv_boot_x` loads:
`usbmouse`, `ethernet`, `hibernatedrv`, `autostart`, `hibernate`, `fontpick`,
`desksc`, `stikboot`; kern_emu `vmmouse`; kern_mega `megaovl`, `megamod`,
`megahiber`.

**Left undone**: kern_small's on-demand `small.img` is still outside the
canary generator's and `t_canary`'s set (areas/boot.md question 4); the
`os88boot` measurements that would turn this wave's PREDICTED call counts
into MEASURED ones are the final pass's; wave 5.
