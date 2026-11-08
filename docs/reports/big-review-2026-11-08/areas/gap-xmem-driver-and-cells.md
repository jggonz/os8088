# XMEM.DRV (drivers/xmem/xmem.asm) and the kernel's XMS/segment API cells in kernel.asm

Gap review, 2026-11-08. I read the code at the review's tree and ran no build
and no emulator. Line numbers are this tree's. They run about 20 lower in
`kernel.asm` than the brief's, because the tree has moved since the brief was
written. I assembled `drivers/xmem/xmem.asm` once into the scratchpad with
`-l`, using the Makefile's own command line. The output is byte-identical to
`build/xmem.bin` (2,041 bytes), and every image offset below comes from that
listing. Kernel offsets come from `build/kernel.map`. "ESTIMATED" bytes are
hand counts of the instructions named, never a measured span.

## Scope read

- `drivers/xmem/xmem.asm` 1-1788, all of it: the header, `xm_entry`,
  `xm_attach`, `xm_walk`, `xm_caps`, `xm_alloc`, `xm_free`, `xm_relall`,
  `xm_rel`/`xm_tokb`, `xm_chk`, `xm_copy`, `xm_ucopy`, `xm_bios`, `xm_lcopy`,
  `xm_lend`, `xm_lainit`, `xm_ltrip`, the A20 family (`xm_a20_reopen`,
  `xm_a20_probe`, `xm_kbc_a20`, `xm_fast_a20`, `xm_a20_settle`,
  `xm_kbc_wait`, `xm_a20_enable`), `xm_once` and its scratch equs, `xm_hma`,
  `xm_arm` and `xm_gdt`.
- `apps/os88ub.inc` (all), `apps/os88la.inc` (the data layout and `LA_SZ`),
  and `apps/os88xgap.inc` (all).
- `kernel/xmem.inc` (all, both arms), `kernel/kernel.asm` 1940-1975 (tiers,
  `XM_MAX_BLKS`), 2588-2600 (`OSAPI_XREFUSE`), 2750-2770 (the four XMEM
  cells), 4338-4420 (the 0x04B3..0x04E3 cells), and 6236-6585 (`osapi_xmem_sel`/
  `_selc`, `osapi_xmem_run`, `osapi_mem_subseg`, `osapi_seg`/`osapi_segr`,
  `osapi_img_name`, `osapi_xmem_walk`/`_walkc`, `osapi_mem_codesel`/
  `_codeselc`).
- `kernel/wm.inc` 11695-11765 (the nest that `xm_dsp` brackets with) and
  `kernel/hiber.inc` 600-615 (its second `XM_MAX_BLKS` reader).
- Consumers: `drivers/ramdisk/rdstore.inc` 115-140 and 455-535,
  `kernel/driver.inc` 2880-2895, `apps/dos/dos.asm` 3256-3300 (`dos_ivtab`),
  3570-3640 (the CP/M field), 14469-15075 (DOS.O88's XMS), and
  `kerndos/kdhma.inc` 1-80 and `kerndos/kdpark.inc` 160-200 (kern_dos's
  A20 handling, for contrast).
- `apps/os88api.inc` 2040-2215 and 2950-3070 (the XMEM group's prose).
- SPEC.md 41 through 41.16 (83223-84190) and 96.15.2 (183457). The kern_dos
  int 15h table is at 183545-183556.
- `docs/plans/HMA-PLAN.md` 95-120 and 344-352, and `docs/KERNEL-MEMORY.md`
  857.
- Area reports `memory.md` (mem-20, the XM_MAX_BLKS row, doc items T/U/V),
  `drivers.md` (rows 19, 399-407, 546), `abi.md` and `hiber.md`, so that
  this report does not repeat them.
- `docs/plans/ABI-BREAK-LIST.md` and `docs/plans/KERNEL-BYTE-QUEUE.md`,
  searched for XMEM, XMS, A20 and the slot numbers. The hits are T3 and V3.

## Verdict

The store is one of the better-engineered pieces of the kernel.

- It is an overlay behind one gate word, `[xm_kb]`.
- Its tier-0 refusals are byte-identical to an 8088's.
- It refuses unknown verbs by name.
- It lays its scratch over the code that runs only at attach.
- Its allocator merges free runs for nothing.
- It has three transports behind one ABI, and each restores what it borrowed:
  - **int 15h AH=87h**: the PIC mask and the A20 gate;
  - **LOADALL**: the 102-byte cell table and DS/ES;
  - **loaded bases**: DS/ES, reloaded in real mode, with no widened limit left
    behind.

Every arithmetic edge I traced is handled, the round-up carry, the 64MB
ceiling and the KB-span overflow among them. So is every interrupt window: the
cli-published table writes, the per-chunk and per-piece cli, the PIC mask
across the BIOS call and the 8042 sequence under one cli. I found no
correctness bug inside the transports themselves.

The systemic hole is outside the transports, and it is A20 and int 15h
**ownership**. HIMEM.SYS took ownership of two things:

- **The pre-XMS int 15h protocol.** HIMEM answers AH=88h with 0, so an older
  program does not write over XMS.
- **The A20 line.**

kern_big takes neither. It leaves A20 open "for the session" and assumes that
nothing else will touch it. The windowed DOS (DOS.O88) runs foreign programs
on kern_big with **no int 15h hook at all**. Such a program sees the BIOS's
raw AH=88h figure. Its AH=87h can write over every pool block and over
`.cold` and SOUND.DRV in the HMA. On an IBM AT that AH=87h also shuts A20,
which strands every far call into the HMA. kern_mega's kern_dos already has
the right table (SPEC.md 96.15.4's int 15h hook). DOS.O88 does not.

The tier-2 copy also never re-proves A20, while the tier-1 LOADALL path does.
So a gate a third party shut turns a copy into a silent write 1MB lower,
into conventional memory.

Both repairs are small:

- a DOS.O88 vector, which is package bytes only;
- the probe moved above the tier branch, at about zero image bytes.

The other findings are smaller:

- The block table is eight entries shared by the whole system, and DOS.O88
  alone can open eight handles. A stride change grows it to 13 at no image
  cost.
- `OSAPI_XMEM_CAPS` has no largest-run answer, so DOS.O88 reports the total
  free as the largest block.
- Every driver and the kernel share owner 0xFF, and nothing ever sweeps 0xFF.
- The unreal-mode arm (`xm_arm`) is now dead weight that one display label
  reads.
- The kernel's copy cell spends 83 bytes of `.text` and 12 of `.bss` folding
  a parameter block that a register convention would make unnecessary. That
  is a private-ABI change worth about 54 resident kern_big bytes.

Many comments and much SPEC prose still describe two transports, a 1KB image,
`xm_init`, and FS/GS as the tier-2 copy. The doc list below is long because
§41 has been amended in dated sections without its older paragraphs being
revisited.

## Strengths

1. **One gate, tier-0 answers.** `[xm_kb]` means both "there is a store" and
   "the overlay is attached". It is written last at `xmem.inc:342`, after the
   service table is staged (`:300-323`). Every cell tests it through
   `xm_have` (`:398-400`). With no store, the cells answer exactly what an
   8088 answers (`:485-500`, `:533-544`, `:681-683`). So every package has
   already been exercised against the refusal path on the target machine.
2. **The verb fence.** `xm_entry` (`xmem.asm:210-214`) names ATTACH and DETACH
   and sends every other verb to `xm_no`. The `%if` at `:207-209` pins the
   ordering it depends on. This is the lesson of `snd_entry`, kept.
3. **Attach-only scratch.** The block table, the AH=87h block, the copy ends,
   the pool fields and the LOADALL state are 208 bytes. They are laid over
   225 bytes of code that runs only at attach (`xm_once` at image 0x718,
   which runs to the end at 0x7F9). The once-per-image guard is `xm_feat`'s
   CPU_F_A20 (`:259-260`). The layout is asserted by `times` (`:1597-1599`
   and `:1785-1786`). The scratch therefore costs no image bytes.
4. **An implicit-free allocator.** `xm_alloc`'s candidate walk
   (`:539-560`) records no gaps, so a free merges with no code. The search is
   bounded by XM_MAX_BLKS restarts. The candidate's end is tested once per
   restart (`:540-544`).
5. **Edges are handled.**
   - The round-up carry is refused (`:521-523`).
   - Requests of 64MB or more are refused (`xm_tokb`, `:715-718`).
   - `xm_chk` does a 32-bit interval test as two 16-bit KB compares, with its
     own carry check (`:741-791`).
   - `xm_free` refuses an unaligned base before it searches (`:610-614`).
   - The pool clamp for the 24-bit AH=87h descriptors is exact
     (`:297-305`: 15,296KB with the HMA).
6. **Publication under cli.** All three mutators publish under one
   pushf/cli: `xm_alloc` at `:562-567`, `xm_free` at `:618-632`, and
   `xm_relall` at `:672-686`. `xm_relall` runs on the dying task while the UI
   task may be inside `xm_alloc`/`xm_free`, and the comments name the exact
   torn-entry race this prevents.
7. **`xm_bios` restores what the BIOS breaks.** The master PIC is masked
   across the int 15h, because the IBM AT BIOS `sti`s, and the mask is put
   back (`:1036-1061`). The gate is reopened before any interrupt is serviced
   (`xm_a20_reopen`, which is resident, `:1356-1363`). The descriptor loop
   also refuses a nonzero base byte 31..24 (`:1022-1025`).
8. **LOADALL is admitted by evidence.** `xm_lainit` (`:1201-1238`) runs two
   round trips against the BIOS's own transport, with INT 6 hooked across
   them. The pattern and its complement swap the directions. A trap, a
   corrupted word or a cache that does not take all leave `[xm_lall]` at 0.
   Each piece is its own cli window (`:1135-1141`), and the conventional end
   is addressed as itself (`xm_lend`, `:1172-1195`). A cache that did not
   take therefore copies a buffer onto itself and nowhere else.
9. **The loaded bases leave nothing behind.** `xm_ucopy` (`:912-967`) loads
   DS/ES bases per 2KB chunk, inside one cli, and reloads both in real mode
   (`:950-952`). No widened limit outlives the chunk. The GDT base is
   recomputed on every call because the image moves (`os88ub.inc:113-117`).
10. **The 8042 sequence is one cli window** (`:1474-1487`), and the comment
    names the reboot race it closes. Port 0x92's bit 0 is masked
    unconditionally, and a read of 0xFF is declined (`:1497-1510`).
11. **The kernel stamps the owner.** `xm_ownbl` (`xmem.inc:460-465`) reuses
    `snd_req_inst`, so the image needs no API slot to ask. The
    `XM_ABI_VER` handshake (`:62`, `xmem.asm:121`, checked at `:261-263`)
    turns a half-copied floppy into a refusal at load.
12. **The real-mode `OSAPI_SEG`** (`kernel.asm:6394-6426`) answers each verb
    with the caller's own arithmetic in about 30 bytes. Every verb above
    SEG_BWIN is refused by one `ja`, so new protected-mode verbs cost real-mode
    kernels nothing.

## Findings

### xm-1. kern_big does not own int 15h: a DOS program can write over the pool and the HMA, or shut A20 (hazard, medium, now)

- **No hook.** `dos_ivtab` (`apps/dos/dos.asm:3256-3300`) hooks 20h-2Fh and
  33h, and not int 15h. Nothing in `kernel/` hooks int 15h on kern_big either.
  The one hook is `megados.inc:1742`, which is kern_mega's.
- **What the program sees.** A DOS program under DOS.O88 on a 286 or later
  sees the BIOS's raw AH=88h figure, which is the whole of extended memory.
  It can then AH=87h anywhere above 1MB:
  - the pool blocks of every instance;
  - the RAM disk store;
  - `.cold` and the drivers XMEM.DRV put in the HMA (`xm_hma`,
    `xmem.asm:1643-1689`).
- **A20.** On an IBM AT the BIOS's AH=87h also shuts A20 on its way out, as
  the code's own comment at `xmem.asm:1037-1050` says. The next IRQ0 then
  reaches `snd_tick` and SOUND.DRV in the HMA with the gate shut, which is
  the lock-up `xm_bios` was written to prevent. Port 92h, the 8042 and int
  15h AH=24h are three more ways in.
- **Already half-recorded.** `docs/plans/HMA-PLAN.md:113-119` records the A20
  half as "not covered". It rests on "the same class of hazard as a DOS
  program writing to kernel memory".
- **It is not the same class.** A 1987 program using the int 15h extended
  memory protocol is behaving *correctly*, and HIMEM.SYS exists to make it
  safe. HIMEM answers AH=88h with 0 so that the program takes XMS or nothing.
  kern_mega's kern_dos already carries exactly that table (SPEC.md
  183545-183556):
  - AH=88h gives AX=0;
  - E801h/E820h/C7h/8Ah give CF=1 AH=86h;
  - an AH=87h touching the HMA or the kernel gives CF=1 AH=03h.

**Recommendation.** DOS.O88 should hook int 15h for its programs with the
kern_dos table, cut down:

- AH=88h answers AX=0, CF=0.
- AH=87h is refused (CF=1, AH=03h) when either descriptor base is at or above
  0x100000.
- E801h, E820h, C7h and 8Ah answer CF=1, AH=86h.
- AH=24h answers AH=86h.
- Everything else chains to the BIOS.

That is about +45-60 bytes of DOS.O88 (ESTIMATED) and no kernel byte. The
reconnect is the same dos_ivtab entry pattern as the others. A row:
`tests/dostrap/xmsprobe.asm` already exists for kern_mega, so run it under
DOS.O88 on a QEMU 386 (QEMU list entry 1).

### xm-2. The tier-2 copy never re-proves A20, so a shut gate becomes a write 1MB lower (hazard, medium, now)

- **The asymmetry.** `xm_copy` probes the gate only on the LOADALL arm
  (`xmem.asm:838-842`). The tier-2 arm calls `xm_ucopy` directly (`:837`,
  `:849`). `ub_set` puts the base on the bus as it is, and with A20 shut that
  base aliases 1MB down without a fault (`os88ub.inc:44-45`).
- **The consequence.** A pool block at 0x110000 is then written at 0x010000,
  inside the conventional heap.
- **The false premise.** `os88ub.inc:45-47` reasons that "no tier-2 path ever
  calls the BIOS's block move, which is the only thing in the tree that might
  close it". xm-1 shows that is false: foreign code runs in the tree. Some
  386 BIOSes also close A20 after their own AH=87h.
- **Where it matters most.** With `.cold` in the HMA the machine dies anyway.
  On a 386 where COLDHMA.SYS is missing, or `.cold` is ROM-resident, the
  failure is silent corruption.

**Recommendation.** Hoist `call xm_a20_reopen / jc .e1` above the tier
branch, and delete the LOADALL arm's `call xm_a20_probe / jc .bios`. The
reopen clobbers only AX, BX, CX and DX. By that point the two ends are
already stored in `[xm_src]`/`[xm_dst]`, and BP is the count, so nothing live
is lost.

- Bytes: about ±0 of image.
- Cost: one probe per copy, roughly 40 clocks on a 386.

The HMA walk door (`xm_walk`, `:404-406`) has the same dependency and gets it
for free if `osapi_xmem_walk` goes through the same reopen. That is a +3 byte
call.

### xm-3. Eight block-table entries serve the whole machine, and the table can be 13 at no image cost (weakness, low, now)

- **Who draws on it.** `XM_MAX_BLKS` is 8 (`xmem.asm:160`,
  `kernel.asm:1960`). The consumers are the RAM disk, the cloner, the Video
  Player's hold and bank, RealDMG, VBattle, PiXEL, XT-MP3, the parts embed's
  OP_XMS, WIN16 and C64. On top of those, DOS.O88's XMS hands a program up
  to `XMS_NH` = 8 handles (`dos.asm:14487`).
- **The failure.** Two or three packages and one DOS program exhaust the
  table with most of the pool free, and the program sees "out of handles".
  HIMEM's default is 32.
- **Why it can grow for free.** The table lives in the scratch over the
  attach-only code (strength 3). It costs no image bytes up to the code's
  225, and today the scratch is 208.
- **The stride is padding.** `XM_BLKSZ` is 8 (`:178`), but the entry is 6
  bytes (+0 OFF, +2 KB, +4 OWN, +5 always 0), and `xm_walk` copies exactly
  those six (`:410-422`).
- **The arithmetic.** At stride 6, 13 entries are 78 bytes against 64 today,
  so the scratch goes 208 -> 222, still under 225. The image stays at 2,041.
  `xm_lainit`'s test buffers need 56 bytes of table, so 78 is enough.

**Recommendation.**

- Set `XM_BLKSZ` to 6 and `XM_MAX_BLKS` to 13 in both files. t_mirror holds
  the pair.
- Fix `tests/xmcheck.py:55` and `tests/xmla.py:92`.
- `hiber.inc:610` and both arms of `xm_caps` read the constant and need no
  edit.

Bytes: 0 image, 0 kernel. With xm-8 the table could grow further.

### xm-4. CAPS has no largest-run answer, and DOS.O88's XMS 08h reports the total as the largest (weakness, low, now)

- **The cell.** `xm_caps` answers free KB as the pool less every block
  (`xmem.asm:488-505`). That is a total, not a contiguous figure.
- **The consumer.** DOS.O88's 08h (`dos.asm:14623-14630`) returns that one
  number as both the largest and the total, "for one pool with one free run
  is the same number".
- **The failure.** Once the pool has a hole, a program that allocates the
  "largest free block", which is the common XMS idiom, gets A0h from 09h.

**Recommendation (now, package bytes only).** Compute the largest run in
DOS.O88's 08h from `OSAPI_XMEM_WALK`, which kern_big already answers:

1. walk the at most 13 records into a small buffer;
2. take the gaps between consecutive offsets;
3. add the gap before the first block and the gap after the last.

Bytes: about +50 in DOS.O88 (ESTIMATED), 0 kernel.

**Alternative (flagday).** CAPS could answer the largest run in a register it
now preserves. That is an ABI change, because callers may lean on SI and DI
being kept. It costs about +30 in the driver.

### xm-5. On kern_big, DOS.O88's XMS 0Bh does not check the offset against the handle, so a bad offset can write the program's other block (weakness, low, now)

- **The gap.** `dos_xms_fits` and its four call sites are `%ifdef DOS_MEGA`
  only (`dos.asm:14874`, `:14966`, `:14988`).
- **What remains.** On kern_big the only fence is the kernel's `xm_chk`. It
  passes any range wholly inside *some* block the DOS instance owns.
- **The failure.** A move on handle A, with an offset past A's end that lands
  wholly inside handle B's block, succeeds. It writes B silently, where
  HIMEM returns A4h/A6h/A7h.

**Recommendation.** Take `dos_xms_fits` and the two non-bothx call sites out
of the `%ifdef`. Bytes: about +55 in DOS.O88 (the routine is 45 bytes,
ESTIMATED).

### xm-6. Owner 0xFF is shared by the kernel and every driver, and nothing ever sweeps it (weakness, low, record)

- **The tags.** Blocks are stamped either with an instance slot or with 0xFF
  (`xmem.asm:175`, `:187-191`). The RAM disk's store is 0xFF on purpose:
  "nothing owns 0xFF, so our own detach is what frees it"
  (`rdstore.inc:131-134`). So is the cloner's.
- **Consequence 1.** `xm_free` and `xm_chk` compare only BL, so any 0xFF
  caller may free or copy into any other 0xFF block.
- **Consequence 2.** There is no `drv_release` sweep. `XMV_RELINST` is
  reached only from `xm_release_rec`, which is instance teardown. A block
  that a driver or a kernel path forgets is gone until reboot.
- **It has already cost a bug.** The shared tag misattributed the RAM disk's
  copies (SPEC.md 62.9.10.2, `driver.inc:2884-2895`).
- **Prior art in the tree.** kern_mega stamps driver claims 0C0h | row and
  images FDh (SPEC.md 41.16's walk row). The Task Manager can then name the
  holder, and teardown can sweep by row.

**Recommendation (record).** Have `drv_call`/`drv_cp_call` leave a "driver
row" stamp that `xm_ownbl` turns into 0C0h | row, and have `drv_release`
dispatch `XMV_RELINST` with that tag. `xm_relall` already takes any AL.

Bytes: about +15 `.cold` and +6 `.text` (ESTIMATED). The slot interpretation
of `snd_inst` must not see 0C0h | row, which is why this row is record and
not now.

### xm-7. The tier-1 interrupt-off windows on the slowest AT exceed a serial mouse byte (hazard, low, record, PREDICTED)

- **The stated rule.** `XM_MAX_COPY` is 32,768 so that an AH=87h "does not
  overrun the mouse UART's receive buffer" (`xmem.asm:147-152`).
- **The arithmetic.** On a 6 MHz IBM 5170, a 32KB `rep movsw` is 16,384 words
  at 4 clocks or more, which is at least 11 ms. With the 5170's wait state it
  is nearer 16 ms. The PIC is masked for the whole BIOS call (`:1036-1061`).
  The LOADALL piece of 16KB (`:1083`) is about 5 ms on the Victory 286 and
  about 2x that on a 6 MHz board.
- **The budget.** A 1200-baud 7N1 mouse byte is 7.5 ms, and an 8250 holds
  one byte.
- **Status.** Not measured, and no emulator here times a 286.

**Recommendation (record).** Take one field run on a 286-6 with the VIDDISK
X rows and a moving mouse. If the run confirms overruns:

- the LOADALL piece could drop back to 8KB. The Victory measured that within
  6% of 16KB;
- `xm_bios` could chunk internally at 8KB. Each call costs an IBM AT a CPU
  reset, so that trade is the field's to price.

Bytes: ±0 for the constant, about +10 for internal chunking.

### xm-8. `xm_arm` (unreal FS/GS) is dead weight: one display label reads its bit, and it leaves the GDTR on zeroed memory (rethink, low, now)

- **What it still does.** The tier-2 copy has used loaded DS/ES bases since
  41.15. FS and GS are "still armed once, at attach, because CPU_F_UNREAL is
  published" (`xmem.asm:59-61`, `:1734-1782`).
- **Who reads the bit.** Its only reader in the tree is REDLINE's inspector
  label (`apps/redline/inspmod.inc:1669`). The SDK forbids packages FS and GS
  outright (`os88api.inc:2050-2052`), so no package may act on the bit.
- **The leftover GDTR.** `xm_arm`'s `lgdt [xm_gdt]` (`:1747`) leaves GDTR
  pointing at `xm_gdt`, which is inside the region that attach then zeroes,
  in an image that moves. That is harmless in real mode (`ub_set` reloads
  GDTR on every chunk), but a later reader of SGDT gets garbage.

**Recommendation.** Delete `xm_arm` and `xm_gdt`: 90 bytes of attach-only
code plus the 3-byte call. Leave CPU_F_UNREAL defined and never set, and
retire SPEC.md 41.4's FS/GS rule into a history note.

- The attach-only code becomes 135 bytes against 208 of scratch, so the
  image shrinks from 2,041 to 2,024 bytes. With xm-3's 13 entries it is
  2,038.
- It frees 73 bytes of headroom under the scratch for xm-3 or xm-9.

Bytes: -17 image now, about -90 on disk before lz4. The CMOS NMI dance moves
to `ub_set` alone.

### xm-9. The conventional end is unchecked, so ES=FFFFh reaches past real mode's ceiling into the pool's first 32KB (weakness, low, now)

- **The fold.** The kernel folds the conventional end ES*16+SI with no bound
  (`xmem.inc:650-659`). The driver checks only the extended end (`xm_chk`,
  `xmem.asm:817-818`).
- **The reach.** `ES = FFFFh, SI = FFF0h, CX = 32768` addresses
  0x10FFE0..0x117FDF. That passes the real-mode ceiling of 0x10FFEF and runs
  into the pool, where the first block of whichever instance holds it lives
  (0x110000 with the HMA claimed).
- **What is legitimate.** Everything below 0x10FFF0 is reachable by real-mode
  code anyway, and HMA-resident drivers legitimately pass FFFF:xxxx buffers.
  So the fence must refuse only the part *past* 0x10FFEF, not "at or above
  1MB".

**Recommendation.** In the driver, after the length checks, compute the
conventional end plus the length. Refuse with AX=2 when the result is above
0x10FFF0. Bytes: about +12 image (ESTIMATED), which needs xm-8's room to stay
inside 2,048.

### xm-10. The kernel's copy cell spends 95 resident bytes folding a block that a register convention avoids (size, low, now)

- **The cost.** On kern_big, `xm_copy` is 83 bytes of `.text`
  (`kernel.map` 0xBDA-0xC2D). `xm_cblk` is another 12 of `.bss`
  (`xmem.inc:739`).
- **The reason given.** The block exists because "drv_call loads ES with
  KERNEL_SEG ... and there are not seven [registers] to spare"
  (`xmem.inc:84-90`).
- **The counter-example in the same file.** `osapi_xmem_walk` already passes
  the caller's ES in a register: "the caller's segment rides in DX"
  (`:514-517`).
- **The proposed convention.** For COPY, BX carries only the owner in BL, so
  the registers would be:

  | register | carries |
  |---|---|
  | DX:AX | the extended end |
  | CX | the length |
  | SI | the conventional offset |
  | DI | the caller's ES |
  | BL | the owner |
  | BH | the direction (`neg di / sbb bh, bh`) |

**The kern_big cell under that convention** is `xm_have`, `je`, three pushes,
`xm_ownbl`, `neg/sbb`, `mov di, es`, the `sch_lock` pair, the dispatch and
the refusal. That is about 41 bytes (ESTIMATED), against 83 + 12 today.

**The driver.** Its seven `es:` block loads and the direction compare
(about 33 bytes) become a `mul`-fold of DI:SI and an `or bh, bh`. That is
about 22 bytes, so about -11 image.

kern_mega keeps its block arm (`mx_copy` reads `XMC_`), so the change is
under `%ifndef KERN_MEGA` and bumps `XM_ABI_VER` to 3. That is a private ABI,
with no PKG_FMT or DRV_VER move. `tests/xmla.py` drives the transports, not
`xm_copy`'s entry, but check that.

Bytes: about -54 resident on kern_big (-42 `.text`, -12 `.bss`) and about -11
XMEM.DRV image.

### xm-11. Folding OSAPI_XMEM_SEL/_RUN into OSAPI_SEG verbs saves 12 bytes per real-mode kernel, but works against V3 (rethink, low, flagday)

- **The brief's question: one verb cell.** The pool cells (CAPS, ALLOC, FREE,
  COPY, WALK) cannot share one verb register cleanly. WALK already takes BX
  as its mode (XMW_FIXED/XMW_LONG), and COPY uses every other register. The
  remaining cells are smaller:
  - On every kernel but kern_mega, SEL (0x04B3) and RUN (0x04CB) are 6-byte
    inline refusals (`kernel.asm:4338-4384`, `OSAPI_XREFUSE`).
  - CODESEL is a 6-byte inline answer.
- **What folding saves.** Folding SEL and RUN in as `SEG_XSEL`/`SEG_XRUN`
  verbs costs the real-mode arms nothing, because `ja .no` at `:6403` already
  refuses every verb above SEG_BWIN. It saves 12 bytes of table on kern_big
  and kern_small, and adds about 6 bytes of `.mega` dispatch above 1MB.
- **Why it conflicts.** ABI-BREAK-LIST V3 proposes the opposite: promote both
  to hot 8-byte slots, because the Video Player calls them per frame on
  kern_mega. OSAPI_SEG is an X-stub RCXCELL and is slower still.

**Recommendation (flagday, record beside V3).** Decide by kern_mega's call
rate. Fold only if V3 is refused. CODESEL should stay inline: it is any-context
and its real-mode answer is `mov ax, dx`.

### xm-12. The kernel cells trust the caller's handle and length, with one exception (weakness, low, record)

These are the brief's validation questions, answered.

| check | where | finding |
|---|---|---|
| ALLOC size | `xm_alloc` | sound: overflow-safe round-up, 64MB refusal, zero-byte request takes 1KB |
| FREE | `xm_free` | sound: exact base, aligned, owner |
| COPY: extended end and length | `xm_chk` and the length gate | sound: wholly inside one owned block |
| COPY: conventional end | the kernel's fold | not validated at all (xm-9) |
| context | every cell | not enforced; see below |

SPEC.md 41.8's context rule says no copies from a worker, because tier 1 is a
BIOS call. No cell enforces it.

- **The driver itself is safe there.** A worker reaching `xm_copy` runs
  under `sch_lock`.
- **The worker's own stack is the risk.** AH=87h borrows the caller's stack
  for its frame, and a worker stack is about 256 bytes.

**Recommendation.** No code change. Name the rule in `os88api.inc`'s
ALLOC/FREE/COPY prose, which states no context today (doc item D22).

## Rethinks

- **Own int 15h and A20 as HIMEM did, but only while foreign code runs**
  (xm-1, xm-2). os8088's "A20 open for the session, nothing closes it" is
  HIMEM's state with DOS=HIGH, and it needs no global or local enable count.
  No os8088 code wants the wrap, and kern_dos LOW handles the program that
  does (`kerndos/kdpark.inc:170-200`). What is missing is the
  **enforcement half**:
  - a DOS.O88 int 15h hook, which is package bytes;
  - a re-proof before every copy, at ±0 bytes.

  A kernel-wide int 15h hook would cost resident bytes on every 8088 for a
  hazard that only foreign code creates.
- **The table's stride and the dead arm decide its size, not a policy**
  (xm-3 and xm-8). Together they give 13 entries at a smaller image. The
  allocator stays first-fit with no compaction, because on kern_big the
  token *is* the address that the in-place readers (`os88la.inc`,
  `os88ub.inc`) dereference. HIMEM-style moveable unlocked EMBs cannot be
  had without kern_mega's tokens, so record that and do not grow the table
  past what the scratch covers.
- **A private-ABI register convention instead of `xm_cblk`** (xm-10): -54
  resident bytes on every kern_big machine, 8088s included, for a vintage
  bump nobody outside the kernel and the overlay sees.

## Prior art

- **HIMEM.SYS (XMS 2.0/3.0).**
  - It hooks int 15h so that AH=88h answers 0 and a pre-XMS program cannot
    overwrite XMS (xm-1).
  - It keeps global and local A20 enable counts. It ships a machine table
    (`/MACHINE:`) for A20 methods beyond port 92h and the 8042, and it
    re-verifies with a wrap compare. os8088's three-step ladder
    (`xm_a20_reopen`) is the AT/PS-2 subset, with a verified probe, which is
    the right core.
  - It defaults to 32 handles, and `/NUMHANDLES` raises that (xm-3).
  - Its handles carry lock counts, so unlocked EMBs can move. kern_big
    cannot do that by design (Rethinks).
  - Its move function takes extended-to-extended and arbitrary even lengths
    in one call, chunking internally. `OSAPI_XMEM_COPY` caps a call at 32KB
    and has no extended-to-extended form, so DOS.O88 refuses that move
    (`dos.asm:14914-14921`).
  - Function 08h reports both the largest block and the total (xm-4).
- **MS-DOS 5 DOS=HIGH.** The low stub makes sure A20 is on before it enters
  the HMA, and FFFF:00D0 carries a jump for CALL 5. kern_mega's kern_dos has
  both (`kdhma.inc:1-35`, `dos.asm:3601-3625`). kern_big's `.cold` in the HMA
  has neither, which xm-2's re-proof partly answers for the copy path.
- **Windows 3.x enhanced mode and OS/2 2.x.** Both virtualise A20 and the HMA
  per DOS VM, so a program's gate games cannot reach the system. That is out
  of reach in real mode. The int 15h table is the real-mode approximation.
- **kern_mega's kern_dos int 15h table** (SPEC.md 183545-183556) is the
  in-tree template for xm-1.

## Doc/code mismatches

| # | where | says | code does | proposed |
|---|---|---|---|---|
| D1 | `drivers/xmem/xmem.asm:4-5` | "one copy routine with ONE ABI over TWO transports" | three transports: `xm_bios`, `xm_lcopy` and `xm_ucopy` (`:837-849`) | "one copy routine with ONE ABI over THREE transports (AH=87h, LOADALL, loaded bases)" |
| D2 | `xmem.asm:89-92` | "It is 2,042 now ... Six bytes are left" | the image is 2,041 bytes (`build/xmem.bin`) | "It is 2,041 now ... Seven bytes are left" |
| D3 | `xmem.asm:99-101` | "the A20 gate, its probe, the HMA loader and the unreal arm are 225 bytes nothing runs after it" | the gate and probe are resident, at image 0x6A0-0x718, before `xm_once`; the region holds `xm_a20_enable`, `xm_hma`, `xm_arm` and `xm_gdt` (0x718-0x7F9) | "the AH=88h question, the HMA loader and the unreal arm are 225 bytes nothing runs after it (the gate and its probe are resident: xm_bios re-opens A20 after every AH=87h)" |
| D4 | `xmem.asm:826-829` | "DX:AX the extended one - which is source and which destination already for extended -> conventional, and swapped for the other way" | the unswapped order stores DX:AX to `xm_dst` and DI:BX to `xm_src`, which is conventional -> extended (XMC_DIR 0); DIR 1 swaps | "DX:AX the extended one - destination and source as they stand for conventional -> extended (XMC_DIR 0), and swapped for the other way" |
| D5 | `apps/os88ub.inc:40-41` | "the GDT's linear base computed from CS at every call" | `mov ax, ds / shl eax, 4 / add eax, ub_gdt` (`:113-116`), which is DS, as `in:` says | "computed from DS at every call" |
| D6 | `os88ub.inc:45-47` | "no tier-2 path ever calls the BIOS's block move, which is the only thing in the tree that might close it" | DOS.O88 runs foreign programs with no int 15h hook (`dos.asm:3256-3300`), and they can call AH=87h/24h or write port 92h | "no tier-2 path of ours calls the BIOS's block move; a DOS program can still shut the gate (docs/plans/HMA-PLAN.md 3), so a caller proves it first" |
| D7 | `apps/os88xgap.inc:30` and `:211` | "XMEM.DRV has 21 bytes of its 2 KB claim left"; "xg_s0: the block's start, 0 apart from DS:0" | 7 bytes are left; the copy's conventional end is the 8KB scratch claim's ES:0 (`:56-71`) | "XMEM.DRV has 7 bytes of its 2 KB claim left"; "the block's start, 0 apart (mod 16 KB) from the scratch's ES:0" |
| D8 | `kernel/kernel.asm:6541-6543` | "owner a slot or FFh, XMW_LONG refused (CF = 1, AX = 2, nothing written), any other BX ignored" | with the HMA tail installed, `xm_walk` answers BX = XMW_FIXED by calling the tail's walk first (`xmem.asm:401-406`) | "...nothing written; BX = XMW_FIXED puts the HMA's contents first when the tail is installed (SPEC.md 28.4.13); any other BX ignored" |
| D9 | `kernel.asm:6291-6293` (`osapi_mem_subseg`) | "in: AX = a paragraph offset ... BL bit 0 = it will be far-called" | os88api.inc:2195-2203 also publishes "BL bit 1 = CX is the window's length in paragraphs" | add "BL bit 1 = CX is the window's length in paragraphs (0 = to the end)" |
| D10 | SPEC.md 83307-83312 (41.4) | "**Only `xmem.inc` writes FS or GS** ... The tier-2 copy re-arms from the resident GDT inside the same `cli` window" | `xm_arm` in drivers/xmem/xmem.asm is the only writer; the tier-2 copy loads DS/ES bases per chunk (41.15) and does not touch FS or GS | "**Only XMEM.DRV's `xm_arm` writes FS or GS** ... The tier-2 copy no longer uses them: it loads DS's and ES's bases per chunk inside one `cli` window (41.15)" |
| D11 | SPEC.md 83548-83550 (41.8) | "on tier 2 it is a pure unreal-mode move (§41.4), and on tier 1 it is `int 15h AH=87h`" | tier 2 is loaded bases (`xm_ucopy`); tier 1 is LOADALL when verified, else AH=87h | "on tier 2 it is a move between loaded segment bases (§41.15), and on tier 1 LOADALL where it verified (§41.13), else `int 15h AH=87h`" |
| D12 | SPEC.md 83579 (41.9 rule 1) | "Nothing above `cpudet.inc` may touch an A20 port" | the gate is XMEM.DRV's (`xm_fast_a20`, `xm_kbc_a20`); kern_dos's `kd_a20shut` and `kdh_tchk` touch it too | "Nothing but XMEM.DRV's gate (§41.12) and kern_dos's own (§96.40.9) may touch an A20 port or assume a gate exists" |
| D13 | SPEC.md 83902-83905 (41.12.3) | "**The image is ONE KB of heap** ... (124 bytes) share an address range with the A20 gate, its probe and the AH=88h question (126 bytes)" | the claim is 2,048 bytes; the scratch is 208 over 225 bytes of HMA loader, AH=88h question and unreal arm (41.12.6) | "**The image's scratch is laid over its attach code** ... (208 bytes) share an address range with the AH=88h question, the HMA loader and the unreal arm (225 bytes; the claim is 2KB, §41.12.6)" |
| D14 | SPEC.md 83986 (41.12.6) | "The claim is `mem_bytes_kb_x` of the image" | `drv_load_at` calls `mem_bytes_gp_x` (`driver.inc:3742`), whole 512-byte granules | "The claim is `mem_bytes_gp_x` of the image (whole 512-byte granules, §50.2.1)" |
| D15 | SPEC.md 84181 (41.16, SEL row) | "`kern_small`'s cell is its `xm_copy`, which answers exactly that refusal" | every kernel but kern_mega answers 0x04B3 inline with `OSAPI_XREFUSE` (`kernel.asm:4339-4343`, `:2595-2600`) | "Every kernel but kern_mega answers it inline (`OSAPI_XREFUSE`: AX = 1, CF = 1)" |
| D16 | SPEC.md 84183 (41.16, WALK row) | "Additive: every other kernel ignores BX and writes 0 at `XMW_CLS`." | kern_big with the HMA tail answers XMW_FIXED with the HMA's contents first (`xm_walk`, `xmem.asm:401-406`; os88api.inc:3010-3020 already says so) | "Additive: every other kernel ignores BX - but kern_big with the HMA tail installed puts the HMA's contents in front for XMW_FIXED (§28.4.13) - and writes 0 at `XMW_CLS`." |
| D17 | SPEC.md 183459-183461 (96.15.2) | "The kernel's own access to memory above 1MB goes through the same BIOS path (§41), so the line is already however it needs to be" | only tier 1 without LOADALL uses the BIOS; tier 2 and LOADALL need A20 open, and `xm_bios` re-opens it | "The kernel keeps A20 open for the session (§41.2, `xm_bios` re-opens it after the BIOS's own move), so the line is already however it needs to be" |
| D18 | SPEC.md 83242 and 83299 | "`xm_init` publishes zero KB"; "the bit also tells `xm_init` where its pool starts" | no `xm_init` exists; the kernel's cells answer tier 0 through `xm_have`, and `xm_attach` sets the base (`xmem.asm:283-288`) | "the cells publish zero KB (`[xm_kb]` stays 0)"; "the bit also tells `xm_attach` where its pool starts" |
| D19 | SPEC.md 84103 (41.14) | "times one 8 KB copy up from DS:0" | `xg_gap` copies up from an 8KB `OSAPI_MEM_CLAIM_DMA_HI` scratch (`os88xgap.inc:56-71`) since MC7 | "times one 8 KB copy up from an 8 KB scratch claim" |
| D20 | SPEC.md 84109 (41.14) | "XMEM.DRV had 21 bytes of its 2 KB claim left then (6 now, §41.12.6)" | 7 now (2,041) | "(7 now, §41.12.6)" |
| D21 | SPEC.md 83543-83545 (41.8) vs `os88api.inc:2953-2959` | "UI-task context" for all five slots; os88api says CAPS is "Any context" | `xm_caps` reads the table and the nest push is ISR-safe (`wm.inc:11720-11742`) | 41.8: "`OSAPI_CPU_INFO` and `_CAPS` any context; `_ALLOC`, `_FREE` and `_COPY` UI-task context" |
| D22 | `apps/os88api.inc:2960-2983` | the ALLOC, FREE and COPY prose states no context | SPEC.md 41.8 binds UI-task context (tier 1 is a BIOS call) | append "UI-task context (SPEC.md 41.8)." to each |

## Open questions

1. Does any 386 profile in `vm/` have a BIOS whose AH=87h shuts A20? That
   decides whether xm-2 is reachable without foreign code.
2. Should DOS.O88's AH=87h *serve* a raw move into a pool block the program
   owns through XMS, as kern_dos does for its arena? Or should it only
   refuse? Refusal is the cheap, HIMEM-equivalent answer.
3. What does a 6 MHz 5170 measure for a 32KB AH=87h, and for a 16KB LOADALL
   piece, with a 1200-baud mouse moving (xm-7)? No emulator here can answer
   that.
4. The Task Manager attributes a kern_big driver's pool block (the RAM disk)
   to FFh, "outside any instance". Is that the wording the owner wants until
   xm-6 lands?
