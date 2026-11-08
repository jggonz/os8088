# Hibernate and the disk cloner

Area key: `hiber`. Reviewer read-only; nothing in the tree was edited but this
file. Byte figures are from `build/kernel.map` as it stood on the branch
(2026-11-08 03:09) and `ls -l build/*.drv`; anything marked *predicted* is
arithmetic, not a measurement.

## Scope read

| file | lines | read |
|---|---|---|
| `kernel/hiber.inc` | 5,767 | all of it - resident half, `HIBER.DRV`, `kds_stub`, the kern_mega PM arm |
| `kernel/hbstub.inc` | 237 | all |
| `kernel/hbstage.inc` | 109 | all |
| `kernel/hbmark.inc` | 116 | all |
| `kernel/diskops.inc` | 3,561 | all - cloner, Write Img, the `.IMG` mount verbs, the resident thunk |
| SPEC.md §87 (158327-158900), §18.99 (40888-41420), §51.11.4-5, §96.45.2, §107.6 | ~1,100 | read against the code |
| supporting code | - | `mouse.inc` (`mou_pall`/`mou_pout`/`mou_lockon`/`mou_isr`/`mou_byte`), `drvvol.inc` (`drv_vol_bank/back`), `driver.inc` (`DRV_ATTACH`, class table), `memory.inc` (`mem_claim` compaction path), `fprog.inc` (`osapi_fs_op`), `ui.inc` (step 0), `kernel.asm` (CSDO macros, `drv_suspend_x` small stub) |
| plans | - | `docs/plans/ABI-BREAK-LIST.md`, `docs/plans/KERNEL-BYTE-QUEUE.md` (items 2, 3), `docs/HEAP-CLAIMS.md`, `docs/reports/CODE-REVIEW-TWO-CYCLES-2026-11-01.md` (nothing in this area except the kern_mega park header row 4, megacore's) |

Build arms: everything in `hiber.inc` is `%ifdef KERN_BIG` (kern_big, kern_emu,
kern_mega); kern_small has no hibernate. `kernel/diskops.inc` builds on every
kernel; `CLO_XMS`, `CLO_RSEQ`'s stream and the `.IMG` mount (`OS88_IMG`) are
kern_big-family only.

## Verdict

This is one of the best-shaped subsystems in the kernel. The resident cost is
genuinely small - **~45 bytes of `.text`, 35 of `.bss` and 287 of `.cold`**
for hibernate (map: `hb_tpl`..`hb_kinit`, `hb_mode`..`hb_susp` + `hb_dosseg`,
`drv_suspend_x`..`hbf_closed`), **15 `.text` + ~27 `.cold`** for the cloner -
against a 7.8KB and a 10KB module. The consistency protocol is right in both
halves (commit record written last, deleted first; boot sector last), the
"same build" rule is what makes the resume stub 440 position-independent bytes
with no relocation, and the stub/stage layout is shared as source between two
hosts with assertions on every overlap that was ever hit. Where it is weakest
is the **wake**: it was built as "the image's kernel set the hardware up the
same way, so nothing needs doing" (§87.6, still printed), and then accreted
four rounds of per-device repair (PIT, speaker, UART, 8042 keyboard, PS/2
mouse) without ever adopting the model kern_mega already has - restore the
image's own 8259 masks - so the repairs undo `mou_lockon`'s silencing of the
losing serial port. The other real defects are unwinds: the kern_big DOS
handoff still has the leak-and-strand refusals that were fixed only on
kern_mega, and the hibernate refusal restores the user's folder from a slot a
driver reload has just overwritten. EMS is the one store hibernate can lose
without refusing. Nothing found here needs an ABI break; everything
recommended is module bytes, 0 resident.

## Strengths

- **The pointer file is a commit record, and the protocol is crash-consistent
  by construction.** Old pointer deleted before a byte is written
  (`hiber.inc:1853`), image, then pointer (`:1928-1938`); a write that dies
  anywhere leaves no pointer, so no resume. The cloner applies the same idea
  the other way round: LBA 0 last (`clo_fin`, `diskops.inc:2068`), so an
  interrupted clone never claims to be the source. This is the journaling
  insight in two forms, and it is the right thing to copy into any future
  multi-sector write that must be atomic to the user (installers, `SYSTEM.CFG`).
- **One description of the fixed head** (`hbm_ptrfix`, `hiber.inc:5654`):
  writer and reader use the same object; the file *is* the object
  (`hbm_wrimg` writes it from CS). Drift between a writer's and a reader's
  layout is impossible, and `%if $ - hbm_ptrfix != HBP_SIZE` pins it.
- **`hb_pick` is THE predicate** (`:522`): menu greying, boot probe, window and
  write target all ask it, so §47 rule 5 holds structurally, not by review.
- **Same-build as the enabling constraint, stated.** The resume overwrites the
  running kernel with byte-identical code, so the stub needs no symbols, and
  `HBP_BUILD`/`MOD_STAMP`/`[api_coldseg]>>8` refuse everything else. This is
  cheaper and more honest than Windows 2000-style versioned hiberfil formats.
- **The staging area is memory no rung owns** (the text framebuffer,
  `hbstage.inc`), bounded by `HS_CODE_MAX`/`HS_STACK` asserts; and it also
  bounds itself against high conventional RAM: an image tops out at `[mem_top]`
  and the stage is at B000/B800, so a 704KB Hercules or 736KB CGA machine
  still fits exactly.
- **The programs' sum is checked by the kernel it restored** (`hbm_psum`,
  `:1521`; wake 3a `:4719`), the same routine on both sides, nothing staged.
- **Job state lives in the buffer it was going to claim** (`CL_*`,
  `diskops.inc:107-155`): a machine that never clones pays 2 bytes plus the
  13-byte name. Copy this for every transient feature that needs a scratch
  block.
- **Disjoint error alphabets** (`CERR_*` from 1, `CLA_*` from 0x80,
  `diskops.inc:64-85`) and the explicit "every verb exit is CF=0" rule turn a
  forgotten `jc` into a wrong answer rather than a plausible one.
- **Hostile-input discipline where it matters**: `hbm_geom`'s range windows
  (`:1693-1701`), `clo_mruns` bounding the chain by cluster range and by the
  file's own sector count so a cyclic FAT cannot loop (`diskops.inc:3344`),
  `clo_geom`'s positive last-sector read.
- **`hbm_extat` with a skip and a take** serves both the image and the DOS
  part read - one walker, one off-by-one surface.

## Findings

Ranked. `phase`: **now** = no ABI break. Bytes are signed estimates; "module"
means HIBER.DRV or DISKOPS.DRV image bytes, never resident.

### hiber-1 (bug, medium, now) - the wake re-arms the LOSING serial port, and kern_big keeps the fresh boot's 8259 mask

`hbm_wake` step 3d writes MCR and **IER = 1 through `mou_pall`**
(`hiber.inc:4801-4807`), and `mou_pall` writes **both** UART rows
(`mouse.inc:474-481`). The image's session had already silenced the loser:
`mou_lockon` writes IER = 0 to every port but `[mou_port]` and masks the
loser's line when it differs (`mouse.inc:1234-1260`). On the reboot route the
8259 mask that comes home is the **fresh boot's** - `hbm_handover` samples it
(`hiber.inc:3708-3709`) and the stub restores it (`hbstub.inc`, `.done`) -
and a fresh boot that has not yet seen a packet has both serial lines open.
So after any resume on a machine with a mouse on one UART and something on
the other (a modem, a serial printer, a second mouse that lost), the loser
asserts its line on every received byte and `mou_isr` scans and discards it
(`mouse.inc:3717-3750`, `mou_byte`'s `jne .jbout`). Harmless for the pointer,
not for the CPU: a chattering 2400-baud modem is ~240 interrupts a second the
session had turned off. §96.45.2's "every one of them is idempotent" on the
reboot route (SPEC.md ~190262) is false for exactly this port.
**Recommendation**: after the IER write, `cmp byte [mou_seen], 0 / je` over
`mov bp, mou_lockon / call hbm_kd` - the resident routine that already does
the job; it re-silences the loser and masks its line (and on a serial win
calls `mou_p2_off`, which the keyboard block below then follows with its
`0xAE`, so place it before `.kbunm`). See hiber-14 for the general mask.
Bytes: **+10 module, 0 resident**.

### hiber-2 (bug, medium, now) - kern_big's DOS-handoff refusals leak the extent claim and strand the user's folder

`HBM_BADV` is `.badv` on kern_mega and **`.bad` on everything else**
(`hiber.inc:3395-3399`). `.badv` (`:3454-3463`) frees `[hb_xseg]` and calls
`hbm_vbk`; `.bad` does neither. The comment there says the fix was for
"every retry of a refused handoff leaked a claim and left the user in
DOS.O88's folder" - and kern_big, the shipped kernel, still does both:
`.b2`-`.b6` are all after `hbm_vbn` (`:2876`), and `.b6` ("kern_dos too
scattered") is after the `MEM_K_HIB` claim (`:2923-2928`), up to
`HS_XMAX*6` = 7.5KB rounded to the granule. `.b4` (`hbm_geomd` refuses: DOS.O88
on a RAM disk, a CD, an HTTP volume or an IDE rung-1 disk) is the reachable
one; it leaves the user standing in DOS.O88's folder.
**Recommendation**: assemble `.badv` on every kernel (it names nothing
mega-specific) and make `HBM_BADV` `.badv` unconditionally. **+~20 module**.

### hiber-3 (bug, low-medium, now) - a refused hibernate puts the user back where a driver reload left them

`hbm_hibfail` (`hiber.inc:1982-1994`) runs `hbm_reload` **then** `hbm_vbk`.
`drv_vol_bank`/`back` is ONE static pair that "never nest[s]"
(`drvvol.inc:100-113`), and `drv_load_at` banks into it (`driver.inc:3446`) -
so every row reloaded overwrites the user's banked folder with the root of
`[hb_vol]` (where `hbm_root` moved the machine at step 3), and the final
`hbm_vbk` restores *that*. Reached whenever any hardware driver was swept
(a sound card is enough) and the hibernate is then refused - serial leased,
write error, HMA copy refused. kern_mega's `.b8s` (`:3445-3449`) has the same
order. **Recommendation**: `call hbm_vbk` before `call hbm_reload` in both.
**0 bytes**. (The nesting rule in `drvvol.inc` is itself a prose invariant;
hibernate is the first place it is broken. A `%error`-able check is not
possible, so say it at `hbm_vbn`.)

### hiber-4 (bug, medium, now) - EMS pages are lost across a hibernate and nothing refuses

`hbm_sweep` deliberately keeps `DRVC_EMS` (`hiber.inc:2233-2236`) - right for
a *suspend*, where the board stays powered. SPEC.md 107.6 says plainly that
the board's contents and page registers do not survive a hibernate's
power-off and "a holder across a hibernate is not supported". But `hb_ok`
greys only on XMS blocks (`:605-609`) and `hbm_arm` checks only room and
transport (`:1712-1782`), so a session holding EMS pages (`apps/browser`,
`apps/video`, `apps/vbattle`, `apps/redline` all use `DRVC_EMS`) hibernates
and resumes onto pages that name nothing. That is §87.7's own rule broken -
"refused rather than guessed at".
**Recommendation**: in `hbm_arm` (module, toast; the menu bar cannot afford a
driver call per press), ask `DRVC_EMS`'s `EMSV_CAPS` through
`OSAPI_DRV_CALL`'s body and refuse with `'EMS pages in use'` when free <
total. **+~30 module, 0 resident**.

### hiber-5 (hazard, medium, now) - nothing fences heap motion during the progressive image write

The image is written live through the file layer (`hbm_wrimg` step 5,
`:1886-1918`), low memory first. Any `mem_claim` the write path makes in the
UI task that misses first fit **compacts and then sheds**
(`memory.inc:951-967`), and the file layer does claim - the per-volume FAT
window (`disk.inc:6194-6220`), the read-ahead store (`:7540-7600`). A block
moved or a record freed after its low half was written and before its high
half is a torn image: `mem_tab` and the data disagree. `hbm_psum` notices
only if a *package* record moved; kernel claims and module images are
unchecked. Today it is held by circumstance (the volume's FAT window already
exists after `hbm_forget`) rather than construction.
**Recommendation**: raise `[mem_cp_busy]` across the image write (the
existing "inside a compaction" guard, which makes `mem_claim` refuse outright
rather than move or shed - and a refused FAT-window claim is documented as
harmless, `disk.inc:6190`), drop it before the pointer write. **+~10 module**.
Worth a `tests/hibernate.py` leg with the FAT window dropped before the write.

### hiber-6 (hazard, low, now) - the resume stub's `int 13h` runs through a kernel hook it is overwriting

`hbm_res` tears down with `hbm_down` (panel, `drv_shutdown`, lock,
`vid_reboot`) and `hbm_handover` (int 08h, three masks) - it never calls
`mouse_unhook`. So on an AT (`KBD_I15`, `kernel.asm:1325`) int 15h still
names `KERNEL_SEG:kbm_i15`, and the AT BIOS calls int 15h AH=90h/91h inside
every hard-disk `int 13h` the stub issues - into the kernel image the stub is
overwriting. It survives only because the same-build rule makes the bytes
identical and `kbm_old9` (`jmp far [cs:kbm_old9]`) equal on the same ROM.
`hbm_dosrun` calls `mouse_unhook` for exactly this reason (`:3304-3355`).
§87.6's reason for not doing it - "the resume path never calls
`mouse_unhook`" so the mouse needs nothing - has been void since step 3d
re-arms the UART and the 8042 regardless.
**Recommendation**: `mov bp, mouse_unhook / call hbm_kd` in `hbm_res` after
`hbm_down`, so the stub runs on ROM vectors only. **+6 module**.

### hiber-7 (weakness, medium, now) - Esc on the Resume face deletes the session

`hbm_onkey` maps Esc to button 2 in every mode (`:1366-1382`), and in
`HB_M_RESUME` button 2 is Discard (`:1411-1414`: `hbm_forget`). One key - the
one every other dialog in the system treats as "no, leave it" - deletes a
640KB session with no confirmation. `hbm_kbdrain` (`:2458`) only covers the
*repeat* case. The close box already does the non-destructive thing (files
kept, asked again next boot, `hbf_closed`).
**Recommendation**: in RESUME mode make Esc close without discarding
(the `.close` arm, files kept); Discard needs the click or `D`. Prior art: CUA
and the Mac HIG both forbid a destructive Esc. **~0 to +4 module**.

### hiber-8 (bug, low, now) - a failed no-question resume pins HIBER.DRV for the session

On the DOS arm (`HBP_DOS` = 1) `hbm_ask` posts the resume with no window
(`:2430-2435`). If `hbm_res` then refuses (`.fail`, `:2664-2671`; kern_mega
`.refuse`, `:4487-4498`) it sets `HB_M_RESUME` and calls `hbm_winback`, which
finds no window. `hbf_dropck` drops only a `GONE` image (`:721-729`) and no
window exists to close, so the ~8KB claim stays until the user next opens and
closes Hibernate. Same if `cw_app_launch` fails at `:2462-2464`.
**Recommendation**: in both refusals, `call hbm_win / jnc` over
`mov byte [hb_mode], HB_M_GONE`. **+~8 module**.

### hiber-9 (bug, low, now) - a pointer this kernel can never read is kept and re-loaded every boot

`hbm_ask` takes any `dwf_dskw_read` failure to `.gone` (`:2368-2369`): no
toast, no `hbm_forget`. §87.9 makes FERR_BIG *the* cross-kernel refusal for a
kern_mega pointer met by kern_big - deterministic, so every boot of that disk
probes, reads 6KB of HIBER.DRV off the system disk and says nothing.
**Recommendation**: distinguish FERR_BIG (and a short read already goes to
`.stale`) and send it to `.stale` - `Hibernation: other build`, forget.
Leave genuine I/O errors on `.gone` (a transient read should retry next boot).
**+~6 module**.

### hiber-10 (bug, low, now) - Write Img writes sector 0 over a short disk and says "Wrote"

`clo_imgrun`'s write loop treats a zero-length chunk as "the file ran out"
(`diskops.inc:1905-1908`) and then calls `clo_fin` unless *nothing* was read
(`:1923-1926`). A file that is shorter at Enter than it was at the Open box
(the keystroke gap `CL_FCLUS` exists for) leaves a partial disk with the boot
sector down and `CLA_DONE` - exactly §18.99.5's "a target that mounts and
lies", which §18.99.2 exists to prevent.
**Recommendation**: at `.wdone`, `mov ax, [es:CL_NEXT] / cmp ax, [es:CL_TOT] /
jb` to `.empty`'s `CERR_NOTIMG` (or a CERR_IO). **+~8 module**.

### hiber-11 (weakness, low, now) - Write Img never asks whether the drive can hold the image

`clo_wimg` (`:1612-1654`) plans, sizes, banks and arms with no `clo_fits`, so
a 1.44MB image to a 720KB drive fails 720 sectors in - the case `clo_fits`'
own header says is worth catching "before a single sector moves". It cannot
simply be called: `clo_wimg` sets `CL_SRC = CL_TGT` and `clo_fits` skips a
same-drive operation first (`:414-415`).
**Recommendation**: gate the `clo_same` shortcut on `CL_END != 2` and call
`clo_fits` from `clo_wimg` after `clo_geom_img`. **+~10 module**.

### hiber-12 (weakness, medium, now) - the cloner cannot see the 5150's own disks

"A disk holding a file system nobody here has heard of" (`diskops.inc:9-14`)
is, on the owner's 5150, first of all a DOS 1.x disk: 8 sectors a track, often
single-sided, **no BPB**. The probe ladder is 18, 15, 9 (`:601-614`) - 8 is
never tried, and every candidate assumes two heads (`clo_ask08` result, the
extent read at `:649-657`) - so an unmountable 160KB/320KB disk is
`CERR_IO`. `Write Img` likewise accepts only four sizes, all double-sided
(`clo_imgtab`, `:1730-1734`; `CL_HDS` hard-coded 2 at `:1702`), so the most
common images in the 5150 archive world (160/180/320KB) are `Not a disk
image`.
**Recommendation**: add 8 after 9 in the ladder; when the 2-head extent
fails, retry with 1 head before dropping to 40 cylinders; give `clo_imgtab` a
heads column and three rows (320, 8, 1 / 360, 9, 1 / 640, 8, 2). **+~30
module, 0 resident**.

### hiber-13 (consistency, low, now) - bare `[cs:]` writes beside the CSDO idiom

HIBER.DRV has ~22 `mov/or [cs:x], ...` writes (`:1245-1246` hbm_paint,
`:1824-1834`, `:1929-1930`, `:1984`, `:2535-2551`, `:2615`, `:3106-3157`)
while the rest of the module uses `CSDO`/`CSEG`. kern_big pays nothing either
way; on kern_mega in protected mode each is a `mega_gpw` fault-and-restart
(`mega.inc:4546`) - `hbm_paint`'s on every repaint of the window, `hbm_wrimg`'s
on every PM hibernate, and the `.kbimg` block on the no-park-room route. The
comment at `:2909-2912` already counts the conversion as a fix.
**Recommendation**: convert to `CSDO`/`CSEG`. **0 kern_big; a few bytes of
the kern_mega module per site**.

### hiber-14 (weakness/rethink, low, now) - kern_big wakes with the fresh boot's masks; kern_mega with the image's

kern_mega banks the image's 8259 masks before the picture (`hbm_xplan`,
`:3942-3945`, "the hardware drivers' lines are already masked by the sweep")
and `mega_hbwake` restores them. kern_big has no equivalent: the stub restores
`HS_MASKM`/`HS_MASKS` sampled from the **fresh** boot (`:3708-3719`), and the
wake then patches individual lines (mouse, keyboard, cascade, IRQ12) that
the image's session had set differently. Each per-device repair in step 3d and
the PS/2 block exists partly because of this.
**Recommendation**: bank `in 21h`/`in A1h` after the sweep into module data
(like `hbm_psv`, so it travels in the image), `out` them at wake step 1 before
the `sti`, and let 3d and the driver reload unmask what they own. Same model on
every kernel. **+~14 module**. Do with hiber-1.

### hiber-15 (weakness, low, now) - `hbm_arm`'s room check is kern_big's on kern_mega PM

`hbm_arm` sizes the need as `hbm_imgsize` + 4KB (`:1746-1748`). In protected
mode the file is the conventional part rounded to 32KB chunks **plus** the
extended section (`hbm_xisz`/`hbm_xconv`, the pool's blocks), which can be
megabytes. A PM hibernate without room passes the check, sweeps, locks, writes
and fails as `Hibernate failed` - the right refusal with the wrong reason that
the comment at `:1769-1772` warns against.
**Recommendation**: on the PM arm add `hbm_xconv` + the pool's in-use KB
(`xm_kb` less free) before comparing. **+~20 kern_mega module**.

### hiber-16 (hazard, low, now) - two non-atomic BDA tick reads

`hbm_dosrun` reads `0040:006C/6E` as two words with interrupts on
(`:3146-3149`), and so does `hbm_wake` after its `sti` (`:4647-4648`). A tick
that carries between the reads makes a 65,536-tick (one hour) error in the
clock catch-up. Rare (1 in 65,536 per read), cheap to close.
**Recommendation**: `pushf/cli`..`popf` round each pair. **+6 module**.

### hiber-17 (size, low, now) - `hb_probe_x`'s three zero stores, and its premise

`hb_probe_x` zeroes `hb_resumes`, `hb_xseg`, `hb_dosakb` because ".bss is
whatever the LAST kernel left" (`:392-394`). The map says otherwise: `.bss` is
virtual 9D88h-B27Eh, i.e. file B788h-CC7Eh, inside the zero padding before
`.ovl` at file CE00h - the loader delivers it zeroed, as `kernel.asm:5896`
says. The stores are now `.boot2` bytes (since KERNEL-BYTE-QUEUE item 2
landed), so this is boot-blob headroom, not resident.
**Recommendation**: keep the `dec ax` / `HB_M_GONE` stores (non-zero values),
drop the three zero ones and correct the comment. **-9 `.boot2`**.

### hiber-18 (hazard, low, record) - `hbm_hsave`'s walk trusts the tail's answer

`.sum` is `sub cx, 6 / ja .sum` (`:2077-2079`): a `ROMF_HTAB` that ever
answered CX = 0 walks 64KB. And `HBP_PAD` stores the table's bytes in one byte
(`:2096`), compared at `hbm_ask` (`:2391`) - a table of 43 records or more
truncates the check silently. Both are true of COLDHMA.SYS today, so this is a
note to pin with an `%error` on the table's size in that build and a `jcxz`.
**+2 module**.

### hiber-19 (rethink, low, record) - keep `HIBERNAT.IMG` allocated

Today the image is deleted before every hibernate and after every wake
(`hbm_forget`, `:1580`). The Phoenix `PHDISK` file and Windows 98/2000's
`hiberfil.sys` keep the file allocated and rewrite it in place: room is
established once, the chain is stable (so a defragmented image stays one run
and the stub's ~80 calls stay ~80), and `hbm_arm`'s room arithmetic and the
delete half of `hbm_forget` go. The wake deletes only the pointer. Cost: the
disk space stays held - §87.6 step 5's "640KB of a 32MB disk is not nothing"
is the owner's call. ±0 module bytes (write-at instead of write; no ABI).

### hiber-20 (prior-art/rethink, low, now for one item) - what DISKCOPY did that the cloner does not

DOS `DISKCOPY` (2.0 on) formats the target track while copying when it is
unformatted or of the wrong shape, offers `Copy another diskette (Y/N)?`
reusing the buffer, and `/V` verifies. The cloner never formats (no `AH=05h`
anywhere - FIELD-NOTES 32, and `dskw_fmt_low` is FORMAT.DRV's and modules may
not far-call each other), never verifies, and frees the buffer at the verdict.
**Now**: "write another copy" after a one-trip clone (the whole disk is in the
buffer or the XMS block) is the cheap one - keep the claim at `CLA_DONE`,
re-arm `CLS_TGT`: **+~40 DISKOPS bytes**, and it is the case people clone for.
**Record**: format-while-copying (~+200 module, duplicates the formatter's
track loop and its `AH=18h` media call, which the cloner also lacks -
relevant to FIELD-NOTES 32); verify (doubles the time; optional key).

### hiber-21 (weakness, low, record) - the fresh session can write the disk under the image

The Resume question is asked on a live desktop after a full boot
(`hbm_ask` from `UI_RBQ_ASK`), and `hbm_res` itself runs `cpf_cp_flush_close`
(`:3643`). Anything the user does before clicking Resume - copy a file, run a
program, change a setting - writes the disk the image's kernel and packages
have cached views of. Caches are dropped at the wake and held streams were
committed before the picture (`ui.inc` step 0, `dwsf_commit`), so this is
mostly stale listings - but it is a model difference from every BIOS/OS
hibernator, which resume before the OS writes anything. Not cheap to change
(asking before the desktop means asking before drivers); worth a sentence in
§87.5.

## Rethinks

1. **One wake model instead of per-device repairs** (hiber-1, -6, -14). Bank
   the image's masks; restore the ROM vectors before the stub; let the wake
   restore the masks and call the *existing* resident routines that put the
   session's devices in its state (`mou_lockon`, the driver reload). The
   per-device code in step 3d and the 8042 block then shrinks to what is
   genuinely power-cycle state (the UART's IER after a power-on, the 8042's
   command byte), and a future device needs a rule rather than a paragraph.
   Net module bytes: roughly neutral; risk is in the PS/2 sequence, which has
   field history (KERN-MEGA-PLAN 14.56-14.62) and should keep its bounded
   polls.
2. **Persistent image file** (hiber-19).
3. **Write-another for the cloner** (hiber-20).
4. **Not recommended**: a full-memory checksum. The image is written live, so
   a whole-memory sum refuses good images (§87.4.1 is right); per-sector CRC
   is the controller's; the residual risk is wrong extents, which the sparse
   package sum and the size check cover. Windows 2000's per-page checksums
   solve a paging problem os8088 does not have.

## Prior art

- **APM BIOS suspend-to-disk (Phoenix ZVHDD, IBM ThinkPad `PHDISK.EXE`)** did
  hibernation below the OS, into a **preallocated, contiguous** file or
  partition, and saved device state through the BIOS. os8088 does it at OS
  level with the file system, which is more portable and costs nothing until
  used - but the preallocation idea transfers (hiber-19).
- **Windows 98/2000 `hiberfil.sys`**: persistent file; header signature
  rewritten (`hibr` -> `wake`) on resume so a crash after resume cannot
  re-enter a stale image; resume runs from the boot loader *before* the OS
  touches the disk. os8088's pointer-delete at wake step 5 serves the first
  purpose; the second is hiber-21's model difference.
- **GEOS (PC/GEOS 1.x-2.x)** - the closest peer - never imaged memory: it
  saved **state files**, each application serialising its object tree on
  exit and restoring it at the next start ("restores where you left off").
  That survives a new build, which os8088's image never can; it needs an
  object system os8088 does not have. Recorded, not recommended.
- **DOS 5 `DOSSHELL` task swapper / DESQview / Software Carousel** swapped one
  task's memory image to disk; os8088's DOS handoff images the *whole*
  machine because `kern_dos` takes it all. The swapper's smaller image is not
  available here without a resident kernel during the program.
- **DOS `DISKCOPY`**: format-while-copying, multi-copy, `/V` (hiber-20). What
  os8088 does better: the boot-sector-last commit and the symmetric
  identity check, which DISKCOPY never had (it would happily copy the target
  onto itself on a one-drive machine); XMS one-trip on a 286; image ends.
- **Mac System 6/7 PowerBook sleep** kept RAM powered - not comparable;
  **AmigaOS** and **OS/2 1.x** had no hibernate.

## Doc/code mismatches

1. **SPEC.md:158521** - "Detach every loaded driver whose class is not
   `DRVC_DISK`, `DRVC_FILE`, `DRVC_FILE2` or `DRVC_EMS`". Code also keeps
   `DRVC_CD` (`hiber.inc:2230-2232`) and, on `VID_EXT`, the VESA renderer row
   (`:2216-2221`). Proposed: "...whose class is not `DRVC_DISK`, `DRVC_FILE`,
   `DRVC_FILE2`, `DRVC_CD` or `DRVC_EMS` - and never the VESA renderer's row
   (§120.4)...".
2. **SPEC.md:158687** - "the PIT is in the kernel's mode 2 (the fresh boot's
   `sched_init` set it and nothing on the resume path unset it)". The wake
   reprograms channel 0 and re-seeds `sch_pit_last` before the `sti`, and turns
   the speaker gate off (`:4586-4603`). Proposed: "1. Channel 0 back to the
   kernel's mode 2, divisor 0, and `[sch_pit_last]` re-seeded (the live route
   left the ROM's mode 3, and a DOS program may have left any divisor); the
   speaker gate off; then `sti`."
3. **SPEC.md:158737** - "The mouse needs nothing: the fresh boot's
   `mouse_init` left the UART and the 8042 in the state the image's kernel
   expects ... and the resume path never calls `mouse_unhook`." Step 3d arms
   the drain, writes MCR/IER, unmasks the line, and re-arms the 8042 keyboard
   and PS/2 mouse (`:4797-5056`). Proposed: "The mouse and the keyboard are
   re-armed at step 3d (§96.45.2): the drain, MCR and IER, the mouse's line,
   and on an AT the 8042's keyboard and PS/2 halves, because the live route
   has no `mouse_init` to do it."
4. **SPEC.md:158723** - "The drivers in `[hb_drvmask]` are loaded again". Code
   reloads `[hb_drvmask] | [hb_susp]` and spends `[hb_susp]` (`:5147-5150`,
   §51.11.5). Proposed: "The drivers in `[hb_drvmask]` and those a standing
   `OSAPI_DRV_SUSPEND` took out (`[hb_susp]`, spent here, §51.11.5) are
   loaded again...".
5. **SPEC.md:158665** - "`DS` becomes `KERNEL_SEG`, `SS:SP` becomes the banked
   frame". The stub loads DS from the staged `HS_KSEG` and sets no stack
   (`hbstub.inc`, `.noslave:` - "NOT SS:SP"); §87.3 already says so. Proposed:
   "`DS` becomes the staged `HS_KSEG`, and a far jump lands in `hbm_wake`,
   which builds `SS:SP` itself out of the memory just put back".
6. **SPEC.md:158772 / 158779** - "259 bytes of `.text`, 94 of `.bss` and 365 of
   `.cold`" and "a 5,499-byte image (4,823 on the disk, packed)" (and the
   later "7,079 + 594", "6,095 + 594 bytes of 7,168"). The map now reads ~45
   `.text`, 35 `.bss`, 287 `.cold` (`drv_suspend_x`..`hbf_closed`), `.modh`
   7,795 bytes including the 594-byte `.modhb`, `build/hiber.drv` 6,213 bytes
   packed - an 8KB claim at the 512-byte granule. Proposed: replace the
   paragraph's present-tense figures with the current ones and mark the old
   ones as history ("it was 259/94/365 at first").
7. **SPEC.md:158461** - "`hbm_hib` deletes both files (pointer, then image)
   before it writes a byte". It is `hbm_wrimg` (`:1853`), shared with the DOS
   handoff. Same section family uses `hb_perform`, `hb_wake`, `hb_ask` for
   `hbm_perform`, `hbm_wake`, `hbm_ask`. Proposed: rename to the code's
   symbols throughout §87.3-87.6.
8. **SPEC.md:158558** - "A failure at 3, 3b, 5 or 6 undoes 1, 2 and 4". Code
   also refuses after step 2 when `[ser_row]` is leased (`:1844-1847`,
   `Disconnect serial first`). Proposed: "A failure at 3, 3b, 5 or 6 - or a
   serial port still leased after the sweep (`Disconnect serial first`) -
   undoes ...".
9. **SPEC.md:158409** - "points item 4 of `menu_items_logo` at one of three
   strings". There are five: `menu_s_hibervx` (VESA, `VID_EXT`) and
   `menu_s_hibernr` (kern_mega PM, no pool) besides the three
   (`hiber.inc:588-611`, `menu.inc:2510-2521`). Proposed: "...at one of the
   strings below", and add rows for `Hibernate (VESA)` and
   `Hibernate (No room)`.
10. **SPEC.md:158578 and hiber.inc:1508** - "That is ~2,500 reads for 640KB: a
    few ms on a 4.77 MHz 8088" / "~2,500 reads for 640KB, a few ms". The loop
    is ~25 bytes and ~10 instructions a sample; at the 8088's fetch floor that
    is ~100 clocks, so ~2,560 samples is **~50-60 ms** (predicted, not
    measured) - still inside the owner's 200 ms. Proposed: "...~2,500 reads
    for 640KB of package memory: some 50 ms on a 4.77 MHz 8088 (predicted)".
11. **SPEC.md:41115** - "The kernel owns it as 0FEh". `FPG_NOWN` is 0FEh, "no
    operation is owned"; the kernel's owner id is 0FFh (`fprog.inc:80-83`,
    `osapi_fs_op`), as `clo_opgo`'s comment says. Proposed: "The kernel owns
    it as 0FFh".
12. **SPEC.md:40941** - "The buffer starts at `CL_BUFEND` paragraphs into the
    claim". No such symbol: it is `CL_HDRPARA` = 64 (`diskops.inc:155, 900`).
    Proposed: "The buffer starts `CL_HDRPARA` (64) paragraphs into the claim".
13. **SPEC.md:40934** - "| `CL_CHUNK` | sectors the buffer holds |".
    `CL_CHUNK` is sectors one trip moves; `CL_STAGE` is what the buffer holds
    (`diskops.inc:121-123`; `clo_xmsup` sets CHUNK to the whole disk). Proposed:
    "| `CL_CHUNK` | sectors one trip moves | `CL_STAGE` | sectors the buffer
    holds |", and note the table lists the disk-to-disk fields only.
14. **SPEC.md:41091 and diskops.inc:194-196** - "18.99.7 The four verbs" / "so
    four verbs cost one mod_fp slot". `clo_vtab` has six verbs, ten with
    `OS88_IMG` (`diskops.inc:308-312`). Proposed: "...so every verb - six, ten
    on kern_big - costs one `mod_fp` slot" and retitle "The verbs, and the
    freeze".
15. **SPEC.md:185104** - "`DRVC_DISK`, `DRVC_FILE` and `DRVC_NET` stay mounted
    across a suspend because a fullscreen program wants them: memory and the
    ROM's `int 13h`, the RAM disk, and the card". `DRVC_FILE` is the cable
    (NET.DRV); the RAM disk is `DRVC_FILE2`; `DRVC_CD` and `DRVC_EMS` also stay
    (`hiber.inc:2222-2238`, `driver.inc:168-209`), and `1 << 8`/`1 << 9` do not
    fit `BL`, so FILE2 and CD cannot be named. Proposed: "`DRVC_DISK`,
    `DRVC_FILE` (the cable), `DRVC_FILE2` (the RAM disk), `DRVC_CD`, `DRVC_EMS`
    and `DRVC_NET` stay mounted ... `BL` can name DISK, NET, FILE and EMS;
    classes 8 and up do not fit a byte and always stay."
16. **apps/os88api.inc:3396-3399** - "DRVC_DISK, DRVC_FILE and DRVC_NET are
    the three the sweep leaves standing - memory, the RAM disk and the packet
    driver's card". Same facts as 15. Proposed: "DRVC_DISK, DRVC_FILE (the
    cable), DRVC_EMS and DRVC_NET are what BL can let go of - classes 8 and up
    (the RAM disk, the CD) always stay".
17. **kernel/hiber.inc:2193-2194, 2200-2202** - "DRVC_FILE is the RAM disk" and
    "the mask can only reach the three rows this loop was already deciding
    about". The loop's own comments (`:2226-2228`) say FILE is the cable and
    FILE2 the RAM disk; the skip list is five classes plus the caller's.
    Proposed: "DRVC_FILE is the cable and DRVC_FILE2 the RAM disk ... so the
    mask can only reach rows this loop was already deciding about".
18. **kernel/hiber.inc:392-394** - ".bss is whatever the LAST kernel left (a
    warm boot clears nothing)". `.bss` is file B788h-CC7Eh, zero padding before
    `.ovl` (CE00h), and `kernel.asm:5896` says ".bss is zero at boot". Proposed:
    ".bss arrives zeroed with the image; the two non-zero values below are
    the only ones that need storing" (with hiber-17).
19. **kernel/hiber.inc:373** - `hb_probe_x` "out: nothing (all registers
    preserved)". It loads BP (`:419`) and leaves it; the comment there says
    kmain_o holds nothing in BP. Proposed: "out: nothing; AX, DX, SI
    preserved, BP clobbered (kmain_o holds nothing in it)".
20. **kernel/hbstage.inc:60-63** - "Above every KDS_* cell (0x0430..0x0441) so
    the two stubs' maps do not alias". `HS_DOSCODE` is 0x043A inside that
    range and `KDS_CPLEN` is 0x043A-0x043D (`hiber.inc:129`); `KDS_KSEG`/
    `KDS_KORG` (0x044A-0x044D, kern_mega), `KDS_CHEAD` (0x044E) and `KDS_FMT`
    (0x0450) are above it. Safe only because the two never coexist. Proposed:
    "The KDS_* cells (0x0430..0x0451) and the resume's cells above HS_SLAVE
    share this band and DO alias (HS_DOSCODE is KDS_CPLEN's low word): safe
    because the handoff stub and the resume stub are never staged together".
21. **kernel/kernel.asm:8348** - "so 0x0550's three verbs answer here".
    `drv_suspend_x` is cell 0x0419 on this branch (`kernel.asm:4040`,
    `os88api.inc:3386`). Proposed: "so 0x0419's three verbs answer here".
22. **docs/plans/KERNEL-BYTE-QUEUE.md:97** - "## 2. `hb_probe_x` → the boot
    overlay — ~60 `.cold`". Done: `hb_probe_x` is `section .boot2`
    (`hiber.inc:385`), `BLOBCALL`ed from `kernel.asm:5966`, and
    `tests/ovlrefs.txt:67` records the move. Proposed: mark item 2 "DONE -
    in `.boot2` (BLOBCALL), not `.ovlw`", keeping the self-overwrite note as
    the record.
23. **docs/HEAP-CLAIMS.md:129** - "| `MEM_K_HIB` hibernate extent list |
    `hbm_xcap`, the resume stub's lifetime |". The tag also covers the HMA
    pool's copy (`hbm_hsave`, lives through the image to wake step 0b), the
    DOS handoff's extent list (claimed before the picture, freed at the wake,
    §87.6.1) and kern_mega's 32KB bounce (`hbm_xbounce`). Proposed: "hibernate
    and DOS handoff scratch: the extent list, the HMA pool's copy (across the
    image), kern_mega's bounce".
24. **docs/plans/HEAP-UNPIN-PLAN.md:626** - "`hbm_detach`/`hbm_reload`
    (kernel/hiber.inc:1321,". The routine is `hbm_sweep` (`hiber.inc:2205`);
    line numbers moved. Proposed: "`hbm_sweep`/`hbm_reload` (kernel/hiber.inc)".

## Open questions

1. **The cwd after a resume.** The image was taken standing in the root of
   `[hb_vol]` (`hbm_root`, step 3) with the user's folder banked in
   `drv_vol_cwd`; the wake never calls `hbm_vbk`, and step 5's reload
   overwrites the bank anyway. Is "the session wakes in C:\" intended? Putting
   it back is a `drv_vol_back` with a cluster from before a power cycle - on a
   floppy that may be a different disk - so the current behaviour may be the
   safe one; say so in §87.6 either way.
2. **Media type on AT drives.** The formatter asks `int 13h AH=18h`
   (`diskw.inc:6199-6236`); the cloner and Write Img do not, and write raw
   onto whatever the BIOS assumes. Is this a candidate cause for FIELD-NOTES
   32 alongside the pinned `int 1Eh`, for a 360KB image written in a 1.2MB
   drive?
3. **`DRVC_HTTP` and the sweep.** Class 10 is not on the skip list, so a
   hibernate unloads HTTPFS.DRV and reloads it from the mask - correct only if
   it can re-attach without the network it was mounted over. Intended?
4. **Hibernate with a mounted `.IMG`.** The DOS handoff refuses a mounted image
   (`hiber.inc:2720-2737`) because a program could replace the host file;
   across a hibernate the fresh session can do the same before Resume
   (hiber-21). Should `hbm_ask`/`hbm_res` re-validate image rows (`clo_mfind`'s
   host + first cluster) or should hibernate refuse while one is mounted?

## Verification

Adversarial pass, 2026-11-08: every bug/hazard and every medium finding
re-read in the code (all build arms); low non-bug findings spot-checked.
Hibernate is kern_big-family only (kern_small has none, `kernel.asm:8347`), so
every hibernate finding is about kern_big, kern_emu and kern_mega. None of
these is already in `docs/plans/ABI-BREAK-LIST.md` or
`docs/plans/KERNEL-BYTE-QUEUE.md`. Machine-readable verdicts and the doc fixes
to apply are in `../data/hiber.json`.

### Findings

- **hiber-1 - CORRECTED (medium -> low).** Narrower than filed. On the live
  route the mask that comes home is the image's, where `mou_lockon` had
  already masked the loser, so IER=1 on a masked line costs nothing. On the
  reboot route a fresh session whose contest had settled (the mouse moved to
  click Resume) masked the loser itself before `hbm_handover` sampled the
  mask. The defect needs a resume reached before the fresh contest settled
  (Enter on the Resume face): the loser then comes home unmasked with IER=1,
  and the image's `[mou_seen]` means `mou_lockon` never runs again.
- **hiber-2 - CONFIRMED.** `.badv` is `%ifdef KERN_MEGA` only; `.b2`-`.b6`
  reach `.bad` on kern_big after `hbm_vbn`, and `.b6` after the claim.
- **hiber-3 - CONFIRMED.** `hbm_reload` -> `hdl_load` -> `drv_load_at` ->
  `drv_vol_bank` overwrites the pair. `deskmod.inc:79-93` already saves it
  around the same nesting.
- **hiber-4 - CONFIRMED.** SPEC 107.6 already says a holder across a hibernate
  is unsupported. What is missing is the refusal.
- **hiber-5 - CORRECTED (medium -> low).** A fragility, not a live defect: the
  write runs with `[sch_lock]` up and the file layer's claims happen at mount
  time. The proposed `[mem_cp_busy]` fence would also make `mem_free` and
  `mem_movable` refuse while it is raised, so check the write path for both
  before adopting it.
- **hiber-6 - CONFIRMED** (low, a latent hazard).
- **hiber-7 - CONFIRMED.** SPEC 87.5 already knows Esc is Discard and only
  drains the keyboard.
- **hiber-8 - CONFIRMED.**
- **hiber-9 - CONFIRMED.**
- **hiber-10 - CONFIRMED.** SPEC 18.99.8's table covers only the case where no
  bytes arrive at all.
- **hiber-11 - CONFIRMED.**
- **hiber-12 - CONFIRMED.**
- **hiber-13 - CORRECTED.** There are 27 bare `[cs:]` stores, and two of them
  (5499-5500) are in the real-mode stub, so about 25 count. The cited
  2909-2912 already use CSDO/CSEG and come out of the evidence.
- **hiber-14 - CONFIRMED** (spot-check).
- **hiber-15 - CONFIRMED** (spot-check).
- **hiber-16 - CONFIRMED.**
- **hiber-17 - CONFIRMED.** On kern_big, kern_small and kern_mega alike,
  `.bss` ends inside the padding before `.ovl`, and SPEC.md 10 states that
  `.bss` arrives zeroed.
- **hiber-18 - CONFIRMED.**
- **hiber-19 - unchecked** (record-only rethink).
- **hiber-20 - CONFIRMED** (spot-check: no AH=05h/18h in `diskops.inc`).
- **hiber-21 - CONFIRMED** (spot-check).

### Doc/code mismatches

Mismatches 1, 2, 3, 4, 5, 8, 9, 10 (SPEC and the `hiber.inc` comment), 11,
12, 13, 14 (heading, body and the `diskops.inc` comment), 15, 17, 18, 19, 20,
21, 22, 23 and 24 are **CONFIRMED**. Their replacement texts were refined, and
4 and 15 were widened to the whole skip list.

- **6 - CORRECTED.** Only the module figure is updated: 7,795 + 594 bytes,
  6,213 packed and an 8,704-byte claim at the 512-byte granule. "8KB" was
  wrong. The reviewer's resident spans leave out the strings, thunks and
  shims that the sentence counts, so the resident figures are left as the
  historical record they are.
- **7 - REFUTED.** `hbm_hib` is a real routine and reaches the delete through
  `hbm_wrimg`, which it calls, so the sentence is true. Renaming
  `hb_perform`/`hb_wake`/`hb_ask` throughout §87 is not a one-place fix.
- **16 - DEFERRED (not applied).** The mismatch is real, but the text is an
  indented comment under `OSAPI_DRV_SUSPEND`'s `%define`, which
  `tools/abilock.py` fingerprints. Editing it fails `make` until someone runs
  `python3 tools/abilock.py --update "comment fixed"`, so it is left for a
  human to apply.
