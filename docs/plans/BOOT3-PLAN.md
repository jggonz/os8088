# `.boot3` — boot-only code that rides in KERNEL.SYS

> **Status: a PROPOSAL. Nothing is built.** Written 2026-11-08 at the owner's
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
> day. Its corrections are made in place, and section 16 lists what it
> checked and what it changed.

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

## 1. The name: `.boot3`

The owner's name is kept, for three reasons.

- **It is literally the third piece of boot-only code by arrival.** Stage 1
  (`boot.asm`) reads `.boot2`. `.boot2` draws the splash and reads
  KERNEL.SYS, which carries `.boot3`.
- **In the end state (wave 4) its entry point is `kmain_o`**, the kernel's
  one-time bring-up. Stage 3 *is* that bring-up.
- **`grep boot[23]` finds every boot-only body in the tree.**

The rejected names:

- `.init` collides with more than a hundred `*_init` routines. Most of them
  (`menu_init`, `inst_init`, `dock_init`) are in `.ovl` and would stay there.
- `.late` says nothing about what the section is late relative to.
- `.btail` collides with KZIP's "tail", which is the whole packed remainder
  of the file (SPEC.md 2.9.13.1), of which this section is only the last
  part.

**The section takes over `.cfx`; it is not a new one.** `.cfx` is already a
`.boot3`: 530 bytes (MEASURED, 0x191A0..0x193B2 in the file). It is the last
section of KERNEL.SYS, expanded by stage 2 into RAM nothing claims until
`mem_unblob_x`, and three blob bodies already far-call it
(`call CFX_RAM:cfx_grid/cfx_ctxcap/cfx_cfgname`). The section is renamed; its
labels keep their names (`cfx_slide`, `cfx_grp`, `cfx_tab`), and so does
`tools/os88cfx.py`.

Names used below:

| name | what it is |
|---|---|
| `B3_START` | the section's start in the file |
| `B3_RAM` = `B3_SEG` | its constant segment |
| `B3_SIZE` | its length |
| `.boot3b` | its nobits scratch |
| `B3_RES` | the length of `.boot3b` |
| `B3CALL`, `B3SECT`, `B2CALL` | the macros (section 4) |

## 2. What the code settled between the three designs

| question | answer | from the code |
|---|---|---|
| Room under the blob today (kern_big) | **1,024 bytes**, not 768 | the packed read ends at 0x600 + 4,608 + R 18,432 + 145 × 512 = **0x18200**; the blob is at BLOB_SEG_AT = 0x1800 + 96 = **0x18600** (kernel.kz.json; kernel.asm BLOB_LIFT). The 768 figure mixed a kernel-relative end with a linear blob address |
| Is `ovl_sc_*` a closed cluster? | **No.** It writes `ovc_buf` through CS: desksc.inc's `ovl_sc_load` does `push cs / pop es / mov di, ovc_buf / rep movsb` | moved alone, CS is `B3_SEG`, and the copy lands at `B3_SEG:ovc_buf` = 0x17DA0 + 0x1A00 = **0x197A0, inside the running blob**. So it moves in wave 2 with `ovc_buf` and the `ovc_*` family, not in wave 1 |
| Ban COLD_SEG in `.boot3`, hop through `[api_coldseg]`, or re-point a fourth group? | **A fourth group.** | Every useful closed cluster names COLD_SEG. Decoding `cfx_tab` (MEASURED): 93 sites in `.text`, 3 in `.ovl`, **25 in the blob**, 192 of 256 bytes used. Wave 1 alone has 11. A ban moves nothing worth having. A hop costs 56 bytes of pair plus dual-arm macros at about 17 sites in shared source. The group costs 6 transient bytes and about 13 ROM bytes |
| Fold `.cfx`, or a separate section at HEAP_SEG? | **Fold.** | The head constraint already exists and is guarded: `%if cfx_grid - $$ < KVGABUF_SIZE`. The slide prefix (`cfx_slide` 71 + `cfx_grp` 26 + `cfx_tab` 256 = **353**, MEASURED) more than covers `.vgabuf`'s **336**. One section means one MODC_START, one tool classification, and no 78-byte gap |
| Guard 5's headroom once BLOB_LIFT is counted | **22,016 bytes** (43 sectors), not 22,272 | 128 × 1,024 − 2,560 − 0x18600 − 6,656 |
| Merge `.ovl` into `.boot3`? | **No.** | `.ovl` lands on the FAT window and `dsk_secbuf`, so it is the one transient that costs no boot RAM, and per byte it costs the same read. It sheds bodies (wave 3); it does not move |
| Does `bvs_apply` move? | **No.** | splash.inc:525-528 runs `BLOBCALL vid_detect` and `call bvs_apply` in the first tick, which is before `.boot3` has been expanded |

## 3. Where it sits, at each moment of the boot

### 3.1 On disk and in the packed stream

`.boot3` is KERNEL.SYS's last section:
`MODC_START equ B3_START + B3_SIZE`. That replaces the `.cfx` arm at
kernel.asm:8981. With the section last:

- `B2_KSECS` (NOKZIP) and `KZ_SECS` cover it with no loader change.
- **It is LZ4-packed** as the end of the last block. kern_big's block 2 is
  30,642 of KZ_BLK 61,440 unpacked (MEASURED), so there are about 30KB before
  a third block.
- `os88kz.py` recomputes R.
- `kz_all`, `kz_expand`, `kz_hd` and the plain head (KZ_HEADSEC 9) are
  unchanged.

**The two-pass identity check (SPEC.md 2.9.13.4) constrains the contents.**
`.boot3` may not assemble any `-DKZ_*` number, or any blob label at or past
`boot2_entry`: the loader's offsets move between the passes, and the check
refuses that loudly.

### 3.2 In RAM

```
B3_START equ (COLD_START + COLD_SIZE + 15) & ~15     ; today's CFX_START
B3_SEG   equ KERNEL_SEG + (B3_START - BOOT2_PAD) / 16 ; today's CFX_RAM, 0x17DA
```

On kern_big this is linear **0x17DA0**. It covers, in order:

1. `.vgabuf`'s rung (336 bytes);
2. the LADDER_PACK rounding up to KERN_END (0x18000);
3. the blob's lift (0x18000..0x18600).

The blob and `ovc_buf` sit above it, at 0x18600..0x1A200.

| moment | what is in `.boot3`'s RAM |
|---|---|
| stage 1, the splash's first tick, the tail read | packed bytes, or nothing yet. **Not callable** |
| `kz_all` returns (end of stage 2, after the canary) | `.boot3`, expanded and aboard |
| kmain → `SPLGATE1 kmain_o` → every kmain_o call | live. `mem_init_x` raises `[mem_base]` past the blob + BOOT2_RES (memory.inc:622-631), so no claim, read-ahead, DIRTYRAM fill or DSK_HCYLSEG buffer can land on it |
| kmain zeroes `[spl_fseg]`, then `mem_unblob_x` | **dropped**: `mov [mem_base], ax` (memory.inc:686) gives back `.boot3`, the lift, the blob and, on a mono machine, the `.vgabuf`/`.vga` rungs, in one store. Then `mem_compact` runs |

That store is the owner's "dropped right before we start dropping the
.vgabuf and other .vga stuff ... and compacting". It is one store, and it
costs **0 new instructions**.

**`.vgabuf` overlap.** On a VGA or EGA machine at home, the planar decoder
owns `.boot3`'s first 336 bytes during kmain_o. The `cfx_grid` comment says
the decoder is used before `cfx_grid` runs. So only the slide prefix may live
there, and it runs only on a machine with neither a VGA nor an EGA. Every
other entry follows a `b3_live` label, asserted `>= KVGABUF_SIZE`. On a
ROM-adopted machine, `rp_walk` has already moved `.vgabuf` to COLD_RAM at
kmain_o's start, so the overlap does not arise there. On a COLDHMA.SYS
machine the same move happens only inside `drv_boot_x`, so until then the
overlap stands exactly as at home; the `b3_live` rule covers both.

### 3.3 The lift is derived, not hand-set

Today `BLOB_LIFT equ 96` (kernel.asm:1952, under `%ifdef ROM_COLD`) is a
guess, and the guard at boot2.asm:546 checks it on pass 2. As `.boot3`
grows, each wave would fight that `%error`. Instead, on the B3 arms only
(kern_small and kern_mega keep today's literal 96, so they stay
byte-identical), with every term in BYTES of linear address and the result
in paragraphs:

```
CEIL512(x)  = (x + 511) & ~511
B3_END      = B3_RAM*16 + B3_SIZE
BLOB_LIFT   = max(96, CEIL512(B3_END + KZ_MARGIN_K + 1022)/16 - HEAP_SEG,
                      CEIL512(B3_END + B3_RES)/16          - HEAP_SEG)
```

CEIL512 keeps the blob 512-aligned (it is a read target), and HEAP_SEG is a
multiple of 32 paragraphs already (KERN_END's LADDER_PACK rounding). Checked
against today's map: B3_END = 0x17FB2, + 1,086 = 0x183F0, CEIL512 = 0x18400,
so the bound gives 0x1840 - 0x1800 = 64 paragraphs and the floor of 96 wins.

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

## 4. How code calls it, and how calls are fenced

**Contract.** A `.boot3` body has exactly a `.boot2` body's contract:

- CS = `B3_SEG`, DS = KERNEL_SEG, SS = LOW_SEG.
- It is far-entered and ends in `retf`.
- Its own data is reached through `cs:`.
- It reaches `.text` through the existing KERNEL_SEG shims (`ovw_*`,
  `spw_near`, `cw_*`).
- It reaches `.cold` by a plain `call COLD_SEG:x`, re-pointed by the fourth
  group (section 4.1).

| macro | on a B3 arm | on every other arm |
|---|---|---|
| `B3SECT` | `section .boot3` | `section .boot2`, or for a former `.ovl` body whatever its arm used: `mem_init_x` is `.ovl` on kern_mega and `.boot2` on kern_small (OVBCALL's arms), so its fallback is OVBCALL's choice, not a fixed `.ovl` |
| `B3CALL x` | `call B3_SEG:x` (5 bytes; OVLCALL's shape: a constant segment, nothing published, nothing retired). Inside `.boot3` it is `push cs / call x` | BLOBCALL (4) or OVLCALL (5), whichever matches the body's home |
| `B2CALL x` | from `.boot3` into a blob entry: `call BLOB_SEG_AT:x` (5). A target that kmain_o reaches by BLOBCALL already ends in `retf`. A target the blob only NEAR-calls ends in `ret` and needs a `retf` (a single-caller body) or a 4-byte `call x / retf` door (a body with near callers that stay, such as `bvs_disp_init`). BLOB_SEG_AT is the very constant stage 1 (HEAP_PARA) and boothd (BLOB_SEG) are built with | — |

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

Every arm that does not carry `.boot3` assembles today's bytes, so kern_small
and kern_mega stay byte-identical. This is OVBCALL/OVSCALL's dual-arm pattern
(kernel.asm's OVBCALL).

### 4.1 The fourth group: who re-points `.boot3`'s COLD_SEG words

| who | when | change |
|---|---|---|
| `cfx_slide` | kmain_o MARK 10, mono machine at home | after the blob's group: `mov ax, B3_SEG / call cfx_grp`, **+6 transient bytes**. `os88cfx.py` emits a fourth group; `cfx_tab` stays about 195 of 256 |
| `rp_walk`, ROM kind (osrom.asm:553) | kmain_o's `call far [10]` (rom_patch), its second act, right after `OVLCALL dsk_boot_from_x` and long after `kz_all`, so `.boot3` is aboard | one more `rp_list` over `rt_b3` at `RT_B3SEG`: `mov ax / mov si / mov cx / call rp_list / jc` is 14 ROM bytes (by encoding), 0 RAM. The site words move from `rt_blob` to `rt_b3`, so the lists' total is unchanged. kern_small's ROM has `RT_NB3 = 0` and assembles no call. No ROM_FMT bump: the lists are the ROM's own, and the ROM accepts only the kernel it was cut from (SPEC.md 2.10.4) |
| `rp_walk`, HMA kind (COLDHMA.SYS) | inside `drv_boot_x`, while `.boot3` is live | the same list. Unlike `rt_win`, it is **not** skipped: `.boot3` outlives the mount |
| `os88rom.py` | build | classifies `.boot3` sites into `rt_b3`. Today it skips `.cfx` whole in its identity compare (os88rom.py:471) because `cfx_tab` is stamped after the assembly; once `.boot3` carries code the ROM re-points, that skip would leave it unchecked, so compare `.boot3` with `cfx_tab` blanked (`os88cfx.blank()` already exists) |

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

The static half is new `tools/os88ovlchk.py` rules. Each lands with a
break-it-on-purpose fixture (docs/WRITING-TESTS.md).

| rule | refuses | catches |
|---|---|---|
| B1 | any reference into `.boot3` except from `.boot2` outside the load-time cone, from `.boot3` itself, or a `tests/ovlrefs.txt` line that answers *"what runs this after `kz_all` and before `mem_unblob`"* | a resident, `.cold`, `.ovl`, module, ISR or CTRL.DRV/HIBER.DRV expansion of a shared macro calling dead RAM |
| B2 | the **load-time cone**: anything reachable from `boot2_entry`, `read_run`, `kz_all`, `kz_hd`, `kz_expand`, `spl_tick` (with `spl_chrome`, `vid_detect`, `bvs_apply` and the `spw_near` targets), `spl_isr` or `spl_anim` naming `.boot3`. Also, `boot/boot2.asm` and `kernel/splash.inc` may not name it at all | first-tick code running packed bytes, the SPEC.md 2.9.4 class. `bvs_apply` is the trap: it looks like "display, not splash" |
| B3 | a CS store, an IVT write or an emitted far pointer naming `B3_SEG` (rule 2b's shape) that outlives the call it is handed to. Two transient hand-downs are legitimate and need a registered exception, not a refusal: DRV_ATTACH's `mov di, %1_att / mov si, cs` (drv_load_at far-calls the hook through DI:SI and returns), and AS_WALK's `push cs / pop es` buffers, which `cw_toast_show` copies and `dsk_find_x` fills | something that outlives kmain |
| B4 | a near call or BLOBCALL across `.boot2`/`.boot3`. `.boot3` becomes the fifth address space in the existing near-call check, beside `.text`/`.vga`, `.cold`, `.blob` and `.ovl`. This includes the KGO_PAIR hops (`call bvs_cgo / dw target`), which read their inline word through CS and so must be in the caller's segment | two segments sharing offsets |
| B5 | a `cs:` reference in `.boot3` naming a label outside `.boot3`/`.boot3b`; and an IMMEDIATE in `.boot3` naming a `.boot2` label that is then read through the blob's CS (`drv_boot_x`'s `add si, bdt_base`, handed to `ovl_spl_msg_drv`, whose `spl_mcs` reads `[cs:si]`), unless registered | the `ovc_buf` trap (section 2), and its mirror, a blob offset carried across |
| B6 | `OVLCALL` from a `.boot3` body that runs after `drv_boot_x`'s mount (rule 2e, extended); a `KZ_*`, `b2_*` or post-`boot2_entry` label in `.boot3` | the forfeited window half; the two-pass identity |
| B7 | `section .boot3` in a file `kerndos/kdos.asm` includes (`disk.inc`, `diskw.inc`, `dskwin.inc`, `hbstage.inc`), unless `kerndos/kdlayout.inc` declares it | an undeclared section, placed silently in the DOS core under `-f bin` |
| B8 | `B3CALL`/`B3SECT` outside the arms that define B3, MACHALF-style | a macro arm that `os88ovlchk` cannot evaluate |

**Runtime half: a knob, not shipped.** Under `B3POISON=1`, `mem_unblob_x`
fills `[floor, blob top)` with a trapping pattern before it compacts, so a
late call faults at a known CS:IP. The new soak row `b3poison` boots it in a
private tree and drives a menu, a file open, a Control Panel round and a
Display switch on MartyPC. To break it on purpose, call a `.boot3` body from
kmain after `mem_unblob_x` and watch the row go red.

## 5. Per arm

B3 is defined iff `KERN_BIG && !KERN_MEGA`, which covers kern_big, kern_emu
and all their knob arms.

| arm | `.boot3` | where |
|---|---|---|
| kern_big, kern_emu (COLD_SLIDE) | yes | B3_RAM = VGABUF_RAM, with the slide prefix at the head |
| NO_COLD_SLIDE knob | yes | same address. The head is a KVGABUF_SIZE zero pad, which packs to nothing. It needs the section anyway: from wave 1 on, the knob sector (SPEC.md 2.5.3.3.1) is not enough to hold what moved |
| NO_ROM_COLD knob (the A/B) | yes | B3_RAM = KERN_END. The file gap spans LOW and `.vgabuf` as zeros: they pack to a few bytes, and nothing in LOW is live during the load. BLOB_LIFT is derived there too, so it stops being 0. `buildmatrix` is the row |
| kern_small | **no**, until the owner says otherwise. `.boot2` has 702 free (MEASURED 4,930 of 5,632), and its disks are only 8 packed sectors from an extra stage-2 call | bodies assemble into `.boot2` through `B3SECT`; `kernel.bin` byte-identical |
| kern_mega | **no**, for now. `.boot2` has 285 free (6,883 of 7,168), enough for BR-1 and BR-6 | byte-identical. Should it opt in, B3_RAM = KERN_END, past `.mega`'s park scratch and `.vgabuf`, and every `.boot3` body is real mode: `mega_enter` runs after `mem_unblob_x` (kmain's order, kernel.asm:6106-6210), so no selector ever names it |

## 6. Per load path

| path | how `.boot3` arrives | note |
|---|---|---|
| Floppy at all four geometries | stage 2's packed read, inside its existing runs | priced in section 8 |
| Hard disk, live USB, live CD (El Torito floppy emulation is the floppy path) | boothd reads `[ksecs]` to KERNEL_SEG + R, and the blob's `kz_hd` expands it in place: the same addresses | **an existing hazard, now more frequent**: an installed disk whose VBR (BLOB_SEG, KZ_RPARA, KZ_HD) predates a KERNEL.SYS with a different lift. Every lift step needs a VBR refresh. Rows: `install`, `hdboot`, `hdtakeboot` |
| NOKZIP | the plain flat read; B2_KSECS follows MODC_START | the `.boot3` guard covers the read's 511-byte rounding |
| FLAT_PAYLOAD | none | no stage 2, so `[spl_fseg]` = 0, kmain_o never runs, and nothing calls `.boot3` |
| 5150 ROM (ROM_COLD, ROM_ADOPT) | always off the disk: the ROM holds only `.cold` | `rp_walk` walks `rt_b3` |
| COLDHMA.SYS / HMA_DRV | off the disk | the HMA kind walks `rt_b3` inside `drv_boot_x` |
| Canary reload (`.rerun`) | re-read and re-expanded with everything else | KSIG_OFF stays put (section 7) |
| Hibernate resume | — | `hb_probe_x` only probes inside kmain_o. The resume runs from `ui_task` long after the drop, and HIBER.DRV's FONT_PICK is its own copy |
| kern_mega's protected-mode switch | — | off on kern_mega. Even on, the switch comes after the drop |
| Knob loaders (BOOTDIAG, TRACKRUN, CYLRUN) | the same file | the same rules |

## 7. The canary, BLOBSUM, and integrity

- **KSIG_OFF stays put.** It is 4,096 on kern_big, inside the 4,608-byte
  plain head, at file sector 21. `.boot3` appends at the far end, and
  BOOT2_SECS stays 13 until wave 4. `t_canary` is unchanged, and so is
  BR-1's fix.
- **The canary moves only if BOOT2_SECS falls.** That is wave 4, and it rides
  boot-10's derived KSIG_OFF (synth/bugs.md BR-1, step 4).
- **A body that moves out of the blob leaves stage 1's BLOBSUM**
  (SPEC.md 2.9.7). Like `.text`, it is then checked by nothing but the decode.
  That is the same exposure the rest of the kernel has: a misread tail sector
  runs bad boot code once. This is stated, not fixed.

## 8. What moves, in waves

Spans are MEASURED off `build/kernel.map`'s `.boot2` and `.ovl` symbols.
Call-site costs are by encoding: BLOBCALL is 4 bytes and a far call 5; a near
call is 3. Each wave is one reviewable commit. Per CLAUDE.md's Testing rule,
no wave runs a soak row: each names its rows, and section 13's union runs
once after the last wave.

### Wave 0: the mechanism, with nothing moved

- `.cfx` becomes `.boot3`: the section line, `MODC_START`, the guards
  (section 13), and the `b3_live` label (today's `cfx_grid` guard).
- Add the macros `B3SECT`, `B3CALL` and `B2CALL`.
- Add the fourth group: `cfx_slide` +6, plus `os88cfx.py`, `rt_b3`,
  `os88rom.py` and `rp_walk` in both kinds.
- Derive the lift (section 3.3).
- **Guard 5 counts the lift.** Fix BR-154 / boot-18: the sum at
  kernel.asm:9703 and `KS_BOOTMX` at :9443 become
  `BLOB_SEG_AT*16 + BOOT2_PAD`. 0 bytes.
- Add the `os88ovlchk` rules B1-B8.
- Add the `kernsize` line: `boot3=` used, free under the blob, and the
  read-end margin. This is WP31's tally, with a third home.
- Teach the tools the section: `os88sym` SEGMENTS (`'.boot3': 'B3_RAM'`),
  `os88hot`, `os88ladder`, `os88boot`, `os88modcost`, `tests/coldslide.py`
  and `tests/bootstatus.py`.

Result:

- **kern_small and kern_mega: `kernel.bin` byte-identical.** `cmp` at one
  commit proves it.
- kern_big and kern_emu: `.boot3` = `.cfx` + 6.
- Resident: 0.

### Wave 1: three closed clusters (kern_big, kern_emu)

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
of boot RAM, 0 resident. kern_small also has `font_init`, but it assembles into
its `.boot2` (`B3SECT`) and stays byte-identical.

### Wave 2: the SYSTEM.CFG reader and the boot driver loader, with `ovc_buf`

`drv_boot_x`'s cone, apart from the status line, `xm_boot_x`, the
adapter-switch bodies it reaches (`bvs_switch`, `bvs_disp_init`, which move in
wave 4) and the title table `bdt_*` (all four are explained below the table):

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

- **`ovc_buf` moves with them**, into `.boot3b`. On the B3 arms BOOT2_RES
  then equals BOOT2_PAD. kern_mega is not a B3 arm and keeps `ovc_buf` past
  its blob, so BOOT2_RES (driver.inc:1054) and `mem_init_x`'s non-small arm
  stay; they become B3-conditional rather than retired. `ovc_buf` is a
  `dwf_dskw_read` destination (`ovc_load`, ES:BX). The disk layer bounces a
  sector that straddles a 64KB page through `dsk_secbuf` (disk.inc's
  `.drvcut`), so alignment is not a correctness rule here, but start
  `.boot3b` on a linear 512-byte boundary anyway (SPEC.md 1's rule, and no
  bounce).
- The 11 COLD_SEG sites move into group 4: `ovl_sc_load` 4, `ovl_sc_free` 1,
  `ovc_unpack` 2, `ovc_load` 1, `drv_boot_x` 1, `bdl_publish` 1, `bdl_hma` 1.
- Crossings, PREDICTED:
  - kmain_o → `drv_boot_x`: +1 in `.boot2`.
  - `drv_boot_x` → `ovl_spl_msg_cfg/drv/boot`, which stay with the status
    line: `B2CALL`, +2 each in `.boot3`. Their `ret` becomes `retf`, 0 bytes;
    kmain_o's other status-line calls already expect `retf`.
  - `drv_boot_x` → `xm_boot_x`: `B2CALL`, +1. `xm_boot_x` stays, because it
    shares `bvs_cgo` with `vx_boot_x`.
  - `ovc_load` → `cfx_cfgname` is now near: −2.
  - kern_emu's `vmm_boot_x` may join (about 100, PREDICTED).
- `os88ovlchk` B1-B6 must certify the closure before the commit. A body it
  rejects stays in `.boot2`.

Result (corrected by the verifier; the first draft moved `bdt_*` and missed
the pair and the doors): `.boot2` frees about **1,640** (1,649 moved, less
kmain_o's +1 and the `bvs_disp_init` door's +4). `.boot3` grows about
**1,715** (1,649 + the 56-byte pair + about +10 of crossings: three status-line
`B2CALL`s +6, `xm_boot_x` +1, `bvs_switch` +1, `bvs_disp_init` +2, with
`cfx_cfgname` near −2), plus 512 nobits. The derived lift goes 96 → **224**
paragraphs (PREDICTED). It is bound by the read: about 0x181B3 + 1,715 =
0x18866, + 1,086 = 0x18CA4, rounded up to 0x18E00.

### Wave 3: relieve `.ovl` (kern_big, kern_emu)

| body | span | caller |
|---|---:|---|
| `vid_ctx_init` | 63 | `OVLCALL` → `B3CALL`, 0 bytes |
| `sch_idle_start` | 35 | `OVLCALL` → `B3CALL`, 0 bytes |
| `mem_init_x` (OVBCALL's kern_big arm; its one `wm_cbd` site leaves group 2 for group 4; it runs after the slide, which re-points both) | 78 | OVBCALL, 0 bytes |
| **total** | **176** | |

All three run before the mount. `.ovl` goes from 1 free to **177**.

**Option**, if BR-157/158 and WP8 want more: the clock ladder from `clk_init`
(one OVLCALL entry, MARK 6). It is **1,782 bytes, MEASURED**: one contiguous
run of `clk_*` symbols in `.ovl`, 0x701 (`clk_init`) to 0xDF7 (`mem_init_x`),
with nothing else between, although clock.inc interleaves `section .text`
blocks in the source. Before moving it whole, check that no rung is reached
from outside `clk_init`'s cone (they are `.ovl` today, so none is reached after
the mount, but B1 asks the same of the drop). Load time: moving `.ovl` bytes
costs about 0 read. `.ovl` is in the same packed stream (file 0xCE00, MEASURED),
and the zeros it leaves behind pack to almost nothing. The lift absorbs about
1.0 byte of R per byte moved.

### Wave 4: the owner's call — `.boot2` back to its original purpose

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
6. **Then `.boot2` is a ratchet.** A new host gate keeps the list of
   non-splash `.boot2` bodies in `tests/blobbodies.txt`, and that list only
   shrinks. That is the owner's "original purpose", enforced by the build.
7. **Then BOOT2_SECS can fall from 13 to 7 or 8.** That needs boot-4 and
   boot-10 (synth/phase1.md WP6), and `t_blobruns`' IMAGES and STAGE2 rows
   move with reasons. On 360KB it is 5-6 fewer sectors before the bar, about
   110-130 ms (PREDICTED). It is still two stage-1 calls there: KERNEL.SYS
   starts at LBA 14 (C0 H1 S6) on os8088-360 and -720, so only a blob of **4**
   sectors or fewer is one call (`t_blobruns.py --sectors 4`, run by the
   verifier). The "6 or fewer" figure is small360's, where KERNEL.SYS starts
   at LBA 12.

## 9. The ledger, per kernel

Today's figures are MEASURED. After-wave figures are PREDICTED. "Free" is
inside the fixed rung.

**kern_big** (kern_emu the same, with its 14-sector blob; there is no emuk
tree here, so its figures are PREDICTED):

| | `.boot2` used / free (of 6,656) | `.ovl` free (of 5,120) | `.boot3` (+ nobits) | BLOB_LIFT (para) | guard 5 headroom | resident | KERNEL.SYS |
|---|---|---:|---|---:|---:|---:|---|
| today | 6,652 / **4** | **1** | 530 (`.cfx`) | 96 | 22,016 | — | 85,168 (167 sectors) |
| wave 0 | 6,652 / 4 | 1 | 536 | 96 | 22,016 | 0 | ~same |
| wave 1 | ~6,148 / **~508** | 1 | ~1,043 | 96 | 22,016 | 0 | ~+450 (+1 sector) |
| wave 2 | ~4,508 / **~2,148** | 1 | ~2,758 + 512 | ~224 | ~19,968 | 0 | ~+1,950 (+4 sectors) |
| wave 3 | same | **177** | ~2,934 + 512 | ~224 | ~19,968 | 0 | ~+1,950 |
| wave 4 | ~3,330 / ~3,326 at 13 sectors (the status line, the loader, the splash, the first-tick probe, and `bdt_*` 92, which stays) | 177 | ~4,170 + 512 | ~320 | grows as BOOT2_SECS falls | **−8** | about net 0 against today once the blob is 7 sectors |

The wave 2 and 3 rows are the verifier's correction (section 8 wave 2):
`bdt_*` stays in the blob, `bxf_sync` moves, and `.boot3` gains its own
56-byte KGO_PAIR. The wave 4 row was not re-derived beyond that.

- **Boot-time arena at 128KB** (from the blob's nobits top, 0x1A200, to the
  top of RAM; the boot sector and its stack are dead by kmain_o): 24,064
  today. After wave 2 it is about 22,528: the lift is +2,048 and
  `ovc_buf`'s move gives back 512. `small128` covers kern_small only, so
  there is no kern_big 128KB row (section 15, question 5).
- **Disk:** the freed blob bytes become zero padding inside the fixed rung,
  so there is no disk saving until wave 4. The cost is 0..1 cluster on a
  system disk per wave.

**kern_small:** `.boot2` 4,930 / 702 free, `.ovl` 1,474 / 62 free. No
section, 0 resident, 0 disk, `kernel.bin` byte-identical in every wave.

**kern_mega:** `.boot2` 6,883 / 285 free, `.ovl` 5,118 / 2 free. No section,
0 conventional and 0 above 1MB, byte-identical. Its `.ovl` will want the
same relief as kern_big's (question 3).

## 10. Load time after the bar (PREDICTED)

**Before the bar, nothing changes in waves 0-3.**

- Stage 1 still reads the fixed 13-sector rung.
- The first tick still follows the 9-sector plain head.
- `.boot3` is the end of the packed tail, which the bar already measures (its
  total is KZ_SECS − KZ_HEADSEC, so it counts the new sectors itself).

**Cost per 512 unpacked bytes moved into `.boot3`.** Each one costs 0.89 to
0.95 packed sectors: 0.886 is the risk design's measurement for blob code
packed after the kernel's history, and 0.95 is block 2's own ratio
(29,096 / 30,642, MEASURED). On top of the read it costs 4.3 ms of LZ4
decode (39.9 cycles a byte, SPEC.md 2.9.13, at 4.77 MHz).

| geometry | ms per sector inside a run | ms per 512 bytes moved | wave 1 (~507) | wave 2 (~1,700) | extra packed sectors before one more int 13h |
|---|---:|---:|---:|---:|---:|
| 360KB (300 rpm, 9 spt) | 22.2 | ~24 | ~+24 | ~+80 | 17 |
| 720KB (300 rpm, 9 spt) | 22.2 | ~24 | ~+24 | ~+80 | 17 |
| 1.2MB (360 rpm, 15 spt) | 11.1 | ~14 | ~+14 | ~+47 | 14 |
| 1.44MB (300 rpm, 18 spt) | 11.1 | ~14 | ~+14 | ~+47 | 16 |

- **The last column** is all three designers' walk of `t_blobruns`' own
  `stage2_calls` model over the shipped images, from today's file. On
  360KB and 720KB the last run is a single sector at LBA 180.
- **Waves 0-3 together use about 4-5 of those sectors**, so there are 0 extra
  calls on every geometry. The 18th sector on 360KB/720KB would cost a call
  (about 170-400 ms, PERFORMANCE.md's per-call price). `t_blobruns`' STAGE2
  ratchet names it at that point, **but it does not fail the build**:
  `blobruns` is registered in the **soak** tier (tests/suite.py), so `make`
  never runs it. It is a 0.1 s host check, so every wave runs
  `python3 tests/unit/t_blobruns.py` by hand with its other host checks
  (allowed per batch under CLAUDE.md's Testing rule: it boots nothing).
  Promoting it to `fast` would make the ratchet the gate this section
  assumed; that tier move is section 15 question 10. The verifier re-walked
  `stage2_calls` with R moved by 0, 1 and 2 sectors as well, and the
  thresholds (17, 17, 14, 16 free) do not move.
- **The room is shared.** Any later kernel growth spends the same 17 sectors.
- **On a 286 the read is track-bounded** (boot2.asm:311), which this model
  does not cover. kern_mega has no rows here.
- **For contrast**, the refused 14th blob sector costs +1 sector *before* the
  bar on every geometry, and on 720KB a whole stage-1 call (4 + 9 + 1).

## 11. What it unblocks in the big review

| item (docs/reports/big-review-2026-11-08/synth/) | where it lands |
|---|---|
| **BR-6**, MDA vs Hercules (bugs.md), alias-test form +60..+110 | **stays in `.boot2`**: it runs in `vid_detect`'s mono arm in the first tick, before `.boot3` exists. `vid_memchk` is written once as a macro and expanded in `.boot2` and `.ovl`. Paid from wave 1's ~508 free (~549 after boot-7). The 3BAh form is no longer forced by room |
| **BR-1**, the canary (bugs.md), +19..+23 | loader: `.boot2`. Fits after wave 1 |
| **BR-147** (bugs.md), +7 per VID_APPLY expansion | `bvs_apply` is the first tick's copy: `.boot2`. Fits |
| **shell-8** (phase1.md, WP27), +10 | the shortcut reader: `.boot2` until wave 2, `.boot3` from then on |
| **boot-17**, kmain_o as a table (phase1.md, WP6) | no longer a byte item. Its −30..−60 were blob bytes only while kmain_o is in the blob. It rides wave 4 for its diagnostics, if wanted |
| **WP6** (phase1.md) | boot-7's −41 is still worth taking, but is no longer load-bearing. boot-18 (BR-154) lands in wave 0. boot-4 and boot-10 gate wave 4's shrink, not waves 0-3 |
| **WP7** (phase1.md), `.ovl` −45 | still lands. With wave 3's 176, `.ovl` covers the +93..+103 wants (WP8 install and gfce-6, BR-18, BR-88, BR-141, BR-157, BR-158, BR-25) with about 120 to spare |
| phase1.md's "14th sector or boot-17" choice | **gone.** The blob wants (+66..+85, or +101..+160 with BR-6's alias form) fit after wave 1 |

## 12. Risks, and what catches each

| # | risk | silent? | caught by |
|---|---|---|---|
| R1 | a body reached from the load-time cone runs packed bytes | yes, and it can show on one geometry only | `os88ovlchk` B2; `splashbar`, `splashspin`, `kzboot` |
| R2 | a missed re-pointer: the slide, the ROM or the HMA leaves a `.boot3` COLD_SEG word stale (a crash on mono after the slide; a call that works by accident after HMA adoption) | yes | `os88cfx` and `os88rom` refuse an unclassified site; `rp_walk`'s look pass; `coldslide`, `rombig`, `romnodisk`, `hmacold`, `hmadrv`, `hmahiber` |
| R3 | a call after the drop (resident code, a module, a stored pointer) | yes | `os88ovlchk` B1 and B3; the `b3poison` knob row |
| R4 | the packed read, or `.boot3b`, overlaps the blob | it dies at once | the derived lift; boot2.asm:546; the new `.boot3b` and NOKZIP read-end guards (section 13) |
| R5 | guard 5 blind to the lift (BR-154, already there today) | yes | wave 0's fix; `small128`, `bootfloor`, `bootfloor-ab` |
| R6 | the two-pass identity: a KZ number or a loader label inside `.boot3` | no (the check refuses it) | `os88ovlchk` B6 |
| R7 | CS-addressed data left behind (`ovc_buf`) | yes, and it corrupts the blob | `os88ovlchk` B5; wave 2 moves `ovc_buf` with its users |
| R8 | a near call between `.boot2` and `.boot3` | yes | `os88ovlchk` B4. MARKW under BOOTMARK=1 near-calls `kmo_mark`, so a body containing one cannot move until MARKW has a `.boot3` arm |
| R9 | `.vgabuf` overlap: a live entry in the first 336 bytes, rewritten by the planar decoder on a VGA machine | yes, and VGA only | the `b3_live` guard; `vgadrop`, `coldpic` |
| R10 | a `.boot3` block in a file kern_dos includes | yes | `os88ovlchk` B7; `kerndos` |
| R11 | installed hard disks: the VBR's BLOB_SEG goes stale on each lift step | it fails at boot | already there today. `install`, `hdtakeboot`; say so in SPEC.md 52.10 |
| R12 | the boot heap at 128KB on kern_big shrinks about 1.5KB by wave 2 | no | `bootfloor-ab` (relative to the floor, so invariant); question 5 |
| R13 | a tool that does not know the section resolves a `.boot3` label against the blob | sometimes | `os88sym` raises on an unnamed section; the others are fixed in wave 0. `os88ladder --selfcheck` is on demand |
| R14 | NO_ROM_COLD and NO_COLD_SLIDE overflow their blob once wave 1 lands | no (`%error`) | the per-arm placement in section 5; `buildmatrix` |

**Found on the way, and not this plan's to fix.** By map arithmetic,
kern_small's blob straddles the 64KB DMA page: linear 0xF200..0x10800, with
the page bound 7 sectors in. Stage 1 bounds its runs at the page
(boot.asm:406-418), but `t_blobruns`' stage-1 model does not. So small360
probably pays 3 stage-1 calls (6 + 1 + 4), not the 2 the row reports
(PREDICTED). The plan's gate change: `t_blobruns` models stage 1's page bound.
Confirm the count with `tools/os88boot.py` on small360 before acting on it.

## 13. Gates and rows

**Assembly-time guards** (kernel.asm, at the foot where the `.cfx` guards are
now):

- `.boot3` is the file's last section: `MODC_START = B3_START + B3_SIZE`.
- `B3_RAM*16 + B3_SIZE + B3_RES <= BLOB_SEG_AT*16`, the nobits scratch.
- NOKZIP's plain read, rounded as the read rounds it: the whole file past the
  blob, not `.boot3` alone, so
  `KERNEL_SEG*16 + ((MODC_START - BOOT2_PAD + 511) & ~511) <= BLOB_SEG_AT*16`.
  (The first draft's `B3_RAM*16 + roundup512(B3_SIZE)` rounds the wrong
  length: B3_RAM is paragraph-aligned, not sector-aligned in the read.)
- On wave 2's arm, `.boot3b` starts on a linear 512-byte boundary
  (`(B3_RAM*16 + ovc_buf) % 512 == 0`), since `ovc_buf` is a disk-read
  destination.
- `b3_live - $$ >= KVGABUF_SIZE`.
- `B3_SIZE + B3_RES <= 0xFFF0`.
- `KZ_MARGIN_K == KZ_MARGIN`.
- Guard 5 and `KS_BOOTMX` on BLOB_SEG_AT.
- boot2.asm:546 is unchanged.

**Host gates**, new or extended:

- `os88ovlchk` B1-B8, with fixtures.
- `os88cfx`'s fourth group.
- `os88rom`'s `rt_b3`, and `romtool --selfcheck` (`model_adopt`/`model_hma`).
- `t_blobruns`:
  - the stage-1 page bound;
  - `--tail N` to price N extra packed sectors;
  - kern_mega rows, so that its thresholds are ratcheted rather than
    predicted.
- `t_bootfloor`: the injected HEAP_PARA and BLOB_SEG equal the map's
  BLOB_SEG_AT, which is what `B2CALL` rests on.
- `kernsize`'s `boot3=` line.
- In wave 4, the `tests/blobbodies.txt` ratchet.
- In wave 0, a `cmp` of `build/smallk` and `build/megak` `kernel.bin` against
  the starting commit, at one commit.

**Rows to NAME** for the single final pass. Run them as one deduplicated
`os88test.py soak -k ...` union, then `test-full` once:

Tiers as tests/suite.py registers them (the first draft filed thirteen soak
rows under fast or full; corrected by the verifier):

- **fast:** `ovlchk`, `canary`, `romtool`, `image`, `diskverify`,
  `livefull`.
- **full:** `small128`, `bootsmoke`.
- **soak, host-only and cheap** (run by hand as each wave's host checks,
  then again in the union): `blobruns` (0.1 s), `bootfloor` (3.5 s),
  `coldpic` (`os88romfix.py --check`).
- **soak, emulator:** `coldslide`, `rombig`, `romsmall`, `romnodisk`,
  `hmacold`, `hmadrv`, `hmahiber`, `smallboot`, `vgadrop`, `buildmatrix`,
  `kzboot`, `kzboot-off`, `hdboot`, `hdtakeboot`, `install`, `cylrun`,
  `splashbar`, `splashspin`, `bootstatus`, `blobsum`, `bootfloor-ab`,
  `autostart`, `hibernate`, `fontpick`, `desksc`, `postboot`, `heapmap`,
  `v20boot`, `pravetzboot`.
- **wave 4 adds:** `vesablitd`.
- **new:** `b3poison` (soak, private tree, knob).

## 14. SPEC.md sections to update first

- **SPEC.md 2.9:** a new subsection, "`.boot3`: the boot's third home". It
  covers the lifetime table of section 3.2, the contract and the macros, the
  arms, the fences, and the derived lift.
- **SPEC.md 2.9.6:** "one transient" becomes three homes, with the question
  each one answers.
- **SPEC.md 2.5.3 and 2.5.3.1:** the rules per half gain `.boot3`'s
  deadline, which is the drop, not the mount.
- **SPEC.md 2.5.3.2:** OVBCALL/OVSCALL arms, and B3SECT.
- **SPEC.md 2.5.3.3 and 2.5.3.3.1:** where kmain_o's body lives; why the
  knob arms no longer need the knob sector for moved bodies.
- **SPEC.md 2.5.1.1 and 2.9.5:** the blob's floor and the derived BLOB_LIFT.
  Guard 5 counts it.
- **SPEC.md 2.9.5.3:** `[spl_fseg]` now also says `.boot3` is aboard.
- **SPEC.md 2.9.13.1:** the file's parts, with `.boot3` last.
- **SPEC.md 2.9.14:** `.boot2` is the loader and the splash, and the
  non-splash residue is a ratchet.
- **SPEC.md 2.10.4, 2.10.7 and 2.10.9:** `rt_b3`, the HMA kind walking it,
  and the slide's fourth group (`.cfx` subsumed).
- **SPEC.md 39.22:** the `.vgabuf` overlap.
- **SPEC.md 15:** the boot sequence's drop point.
- **SPEC.md 52.10:** the VBR-refresh hazard.
- **docs/KERNEL-MEMORY.md**, "The boot overlay" and "Cold code": the third
  boot-only home. `.boot3` is NOT `.cold`: it costs boot RAM and load time,
  never a resident byte.

## 15. Open questions for the owner

1. **The name.** `.boot3`, kept for the reasons in section 1, or something
   else?
2. **Wave 2's boot-time RAM.** Is wave 2's lift of about 4 sectors acceptable?
   It costs 0 resident bytes, about 80 ms on 360KB after the bar, and about
   1.5KB of kern_big's boot-time heap at 128KB.
3. **kern_mega's `.ovl`.** kern_mega has 2 bytes of `.ovl` free and the same
   WP7/WP8 wants. Should it opt into `.boot3` in wave 3, at B3_RAM = KERN_END?
4. **kern_small.** Leave it out, as here, or opt in for one shape in shared
   source? Its packed read is 8 sectors from an extra call.
5. **A kern_big 128KB boot row.** There is none: `small128` is kern_small
   only, and `bootfloor-ab` is floor-relative. Should one be added before
   wave 2?
6. **Wave 4.** Take it, and with it a shorter blob and an earlier bar, once
   boot-4 and boot-10 land? Or stop at wave 3 with the blob at 13 sectors and
   about 2.2KB free?
7. **An earlier bar without wave 4.** The re-homing design observed that on
   360KB/720KB the 9-sector plain head ends on a cylinder boundary. So the
   first tick waits for a full 18-sector tail run, and one `spl_tick` right
   after the head (about 8 blob bytes) would show the bar about 18 sectors
   sooner at any blob length (PREDICTED). Should it be pursued separately?
   It interacts with BR-1's track-bounded head.
8. **The clock ladder.** Should wave 3 move it whole (1,782, MEASURED) or
   only the two clock-card rungs BR-157 and BR-158 touch?
9. **`B2CALL`'s constant segment** (section 4). Take the 5-byte
   `call BLOB_SEG_AT:x` and the `t_bootfloor` fence, or pay `SPLCALL`'s 19
   bytes a site and keep "the blob is wherever `[spl_fseg]` says" true for
   `.boot3` as well?
10. **`blobruns` to the fast tier.** It is 0.1 s and the only gate on "no
    extra stage-2 int 13h after the bar". It is soak today, so a wave that
    spends the 17th or 18th tail sector is not refused by `make`.

## 16. Verification

An adversarial pass on 2026-11-08, against this tree's code and its
`build/kernel.map`, `build/smallk/kernel.map`, `build/megak/kernel.map` and
`build/*kernel.kz.json`. It booted nothing: every check is a reading of
source, a map, or a host model (`t_blobruns`' own functions). Everything
labelled PREDICTED above is still PREDICTED.

### What was checked and held

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

### What was changed in this file

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
   become B3-conditional, not retired.
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
   STAGE2 ratchet does not fail `make`**. Section 10 says so, and question
   10 asks for its promotion.
10. **Wave 4:** a one-call stage-1 blob on 360KB/720KB is 4 sectors, not 6.
    kmain's −4 drops `spl_gate`'s test, which is now said and registered.
    `dkb_geom`'s −4 is confirmed. The KSW_SECT span (614) and the clock
    ladder (1,782) are MEASURED, not PREDICTED.
11. **The 128KB arena** runs to the top of RAM, not to the boot sector, and
    the wording is fixed. Line citations are corrected (BLOB_LIFT is
    kernel.asm:1952).

### What makes it infeasible

Nothing found. Every wave the verifier could check against the code is
buildable as corrected. Wave 2 is the one that changed shape (a 56-byte
second hop pair, `bdt_*` left behind, two doors), and it still frees about
1,640 blob bytes for 0 resident bytes and 0 calls before the bar. Two things
stay open and are not refutations:

- whether `bxf_sync`'s EXF_SYNC closure certifies, for which a door is the
  fallback;
- the size of kern_big's boot arena at 128KB after wave 2, for which there is
  no row (question 5).
