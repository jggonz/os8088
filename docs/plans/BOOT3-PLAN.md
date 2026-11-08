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
> booted.

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
ROM-adopted machine, `rp_walk` has already moved `.vgabuf` to COLD_RAM, so
the overlap does not arise there.

### 3.3 The lift is derived, not hand-set

Today `BLOB_LIFT equ 96` (kernel.asm:1951) is a guess, and the guard at
boot2.asm:546 checks it on pass 2. As `.boot3` grows, each wave would fight
that `%error`. Instead:

```
BLOB_SEG_AT = HEAP_SEG + max(96, roundup32para(B3_RAM*16 + B3_SIZE + KZ_MARGIN_K + 1022) - HEAP_SEG,
                                 roundup32para(B3_RAM*16 + B3_SIZE + B3_RES) - HEAP_SEG)
```

- The 1,022 is the worst case of R's sector rounding plus the tail's.
- `KZ_MARGIN_K` mirrors boot2.asm's KZ_MARGIN = 64, and a guard checks the
  two are equal.
- The formula uses no pass-2 KZ number, and BLOB_SEG_AT is assembled into
  `.ovl`'s DSK_HCYLSEG, so it is pass-stable.
- The floor of 96 makes wave 0 byte-identical. Today's bound would give 64.
- NASM's `%if` cannot see forward references, so the `max` is written
  arithmetically. That is the implementer's detail.
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
| `B3SECT` | `section .boot3` | `section .boot2` (`.ovl` for a former `.ovl` body) |
| `B3CALL x` | `call B3_SEG:x` (5 bytes; OVLCALL's shape: a constant segment, nothing published, nothing retired). Inside `.boot3` it is `push cs / call x` | BLOBCALL (4) or OVLCALL (5), whichever matches the body's home |
| `B2CALL x` | from `.boot3` into a blob entry: `call BLOB_SEG_AT:x` (5). The target already ends in `retf`, because BLOBCALL is `push cs / call`. BLOB_SEG_AT is the very constant stage 1 (HEAP_PARA) and boothd (BLOB_SEG) are built with | — |

Every arm that does not carry `.boot3` assembles today's bytes, so kern_small
and kern_mega stay byte-identical. This is OVBCALL/OVSCALL's dual-arm pattern
(kernel.asm's OVBCALL).

### 4.1 The fourth group: who re-points `.boot3`'s COLD_SEG words

| who | when | change |
|---|---|---|
| `cfx_slide` | kmain_o MARK 10, mono machine at home | after the blob's group: `mov ax, B3_SEG / call cfx_grp`, **+6 transient bytes**. `os88cfx.py` emits a fourth group; `cfx_tab` stays about 195 of 256 |
| `rp_walk`, ROM kind (osrom.asm:553) | kmain_o's first instruction (`call far [10]`), after `kz_all`, so `.boot3` is aboard | one more `rp_list` over `rt_b3` at `RT_B3SEG`, about 13 ROM bytes (PREDICTED), 0 RAM. kern_small's ROM has `RT_NB3 = 0` and assembles no call. No ROM_FMT bump: the lists are the ROM's own, and the ROM accepts only the kernel it was cut from (SPEC.md 2.10.4) |
| `rp_walk`, HMA kind (COLDHMA.SYS) | inside `drv_boot_x`, while `.boot3` is live | the same list. Unlike `rt_win`, it is **not** skipped: `.boot3` outlives the mount |
| `os88rom.py` | build | classifies `.boot3` sites into `rt_b3`. It no longer skips `.cfx` (os88rom.py:471) |

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
| B3 | a CS store, an IVT write or an emitted far pointer naming `B3_SEG` (rule 2b's shape) | something that outlives kmain |
| B4 | a near call or BLOBCALL across `.boot2`/`.boot3`. `.boot3` becomes the fifth address space in the existing near-call check, beside `.text`/`.vga`, `.cold`, `.blob` and `.ovl` | two segments sharing offsets |
| B5 | a `cs:` reference in `.boot3` naming a label outside `.boot3`/`.boot3b` | the `ovc_buf` trap (section 2) |
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

`drv_boot_x`'s whole cone, apart from the status line and `xm_boot_x`:

| body | span |
|---|---:|
| `ovc_map`, `ovc_keys`, `ovc_bit`, `ovc_sig`, `ovc_pack`, `ovc_want_set`, `ovc_unpack`, `ovc_key`, `ovc_deser`, `ovc_load` | 624 |
| `ovl_cfg_load` | 4 |
| `ovl_sc_drop`, `ovl_sc_plain`, `ovl_sc_out`, `ovl_sc_free`, `ovl_sc_load` (shell-8's +10 lands here) | 283 |
| `drv_boot_x`, `drv_boot_want` | 266 |
| `ovl_fdd_apply` (near-called from `drv_boot_x`, driver.inc:5814) | 99 |
| `bdl_load`, `bdl_att`, `bdl_hma`, `bdl_tier`, `bdl_publish`, `bdl_call` | 323 |
| `bdt_*`, the boot driver table (CS data) | 92 |
| **total** | **1,691** |

- **`ovc_buf` moves with them**, into `.boot3b`. BOOT2_RES then equals
  BOOT2_PAD, which retires driver.inc:1054 and `mem_init_x`'s kern_big arm.
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

Result: `.boot2` frees about **1,690**. `.boot3` grows about 1,700, plus 512
nobits. The derived lift goes 96 → **224** paragraphs (PREDICTED). It is
bound by the read: about 0x1885B + 1,086, rounded up to 0x18E00.

### Wave 3: relieve `.ovl` (kern_big, kern_emu)

| body | span | caller |
|---|---:|---|
| `vid_ctx_init` | 63 | `OVLCALL` → `B3CALL`, 0 bytes |
| `sch_idle_start` | 35 | `OVLCALL` → `B3CALL`, 0 bytes |
| `mem_init_x` (OVBCALL's kern_big arm; its one `wm_cbd` site leaves group 2 for group 4; it runs after the slide, which re-points both) | 78 | OVBCALL, 0 bytes |
| **total** | **176** | |

All three run before the mount. `.ovl` goes from 1 free to **177**.

**Option**, if BR-157/158 and WP8 want more: the clock ladder from `clk_init`
(one OVLCALL entry, MARK 6). The re-homing design puts it at 1,782 bytes;
that figure is PREDICTED, because clock.inc interleaves `section .text`
blocks, so the `.ovl` span has to be measured. Load time: moving `.ovl` bytes
costs about 0 read. `.ovl` is in the same packed stream (file 0xCE00, MEASURED),
and the zeros it leaves behind pack to almost nothing. The lift absorbs about
1.0 byte of R per byte moved.

### Wave 4: the owner's call — `.boot2` back to its original purpose

1. **The adapter switch's boot instance (KSW_SECT) moves, apart from
   `bvs_apply`.** That is `bvs_avail_test` through `bvs_kgo`: the re-homing
   design puts it at about 614 (PREDICTED). `bvs_apply` gets a 4-byte
   `call bvs_apply / retf` door in the blob.
2. **`vx_boot_x` + `vx_blk` move** (124 + 58). `mov dx, cs` hands VESA.DRV
   `B3_SEG:vx_blk`, so first verify that VXV_INIT copies the block and keeps
   no DX:SI.
3. **`xm_boot_x` (102), `wm_init` (34) and `ovl_dock_geom` + `ovd_geom` (75)
   move.**
   - `dkb_geom`'s resident `SPLGATE1` becomes a `[spl_fseg]`-tested
     `call B3_SEG:`, **−4 resident**. Its one B1 exception is registered.
4. **`kmain_o` itself moves** (232). kmain's `SPLGATE1 kmain_o` (9 bytes of
   `.text`) becomes `call B3_SEG:kmain_o` (5), **−4 resident**.
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
   110-130 ms (PREDICTED), and `t_blobruns` says a blob of 6 sectors or fewer
   is one stage-1 call there.

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
| wave 2 | ~4,460 / **~2,196** | 1 | ~2,750 + 512 | ~224 | ~19,968 | 0 | ~+1,950 (+4 sectors) |
| wave 3 | same | **177** | ~2,926 + 512 | ~224 | ~19,968 | 0 | ~+1,950 |
| wave 4 | ~3,240 / ~3,416 at 13 sectors | 177 | ~4,170 + 512 | ~320 | grows as BOOT2_SECS falls | **−8** | about net 0 against today once the blob is 7 sectors |

- **Boot-time arena at 128KB** (from the blob's top to the boot sector):
  24,064 today. After wave 2 it is about 22,528: the lift is +2,048 and
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
  ratchet fails the build at that point and names it.
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
| R4 | the packed read, or `.boot3b`, overlaps the blob | it dies at once | the derived lift; boot2.asm:546; the new `B3_RAM*16 + roundup512(B3_SIZE) + B3_RES <= BLOB_SEG_AT*16` |
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
- `B3_RAM*16 + roundup512(B3_SIZE) + B3_RES <= BLOB_SEG_AT*16` (this covers
  NOKZIP and the nobits scratch).
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

- **fast:** `ovlchk`, `canary`, `blobruns`, `romtool`, `coldslide`, `coldpic`,
  `rombig`, `romsmall`, `romnodisk`, `hmacold`, `hmadrv`, `hmahiber`,
  `bootfloor`, `image`, `diskverify`, `livefull`.
- **full:** `small128`, `bootsmoke`, `smallboot`, `vgadrop`, `buildmatrix`.
- **soak:** `kzboot`, `kzboot-off`, `hdboot`, `hdtakeboot`, `install`,
  `cylrun`, `splashbar`, `splashspin`, `bootstatus`, `blobsum`,
  `bootfloor-ab`, `autostart`, `hibernate`, `fontpick`, `desksc`, `postboot`,
  `heapmap`, `v20boot`, `pravetzboot`.
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
8. **The clock ladder.** Should wave 3 move it whole (about 1,782, to be
   measured) or only the two clock-card rungs BR-157 and BR-158 touch?
