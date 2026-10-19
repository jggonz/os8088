# The Video Player on a 386 and better — XMS, EMS and the instructions

**Status: A, B, C and D are BUILT (2026-10-16, branch `video-386+`). M, the
field measurements, is OPEN and waits on the owner's machines. E, more than
64 MB, is DEFERRED by the owner.** SPEC.md 41.15, 52.1.4, 98.3.18.14 and
98.3.18.15 are the contract. This file is the question, the answers and
what is still unknown.

## 1. The question

The owner asked whether the player uses XMS and EMS as well as it can on a
386 or better. Did we use unreal mode, and should we? Did we have the "direct
access" LOADALL gives a 286, and would it help? And which other 386
instructions could the player use without breaking any lower tier?

## 2. What the tree did, before this

- **Unreal mode existed, but only inside XMEM.DRV's copy** (`xm_ucopy`, SPEC.md
  41.4/41.5). FS and GS were armed flat and re-armed every 1 KB. Each re-arm
  was a full CR0.PE transition with NMI masked. The move itself was a
  load/store loop, about 17-19 clocks a dword on a 386 against `rep movsd`'s
  ~4. MOVS had been ruled out because its destination is ES:(E)DI, and a flat
  ES would be a widened ES left behind.
- **The player never addressed XMS itself on a 386.** On a 286 it decoded the
  hold and the bank IN PLACE through LOADALL (98.3.18.11, 98.3.18.13). On a
  386 it always copied: once a byte for the hold, twice for a bank. So a 286
  made zero copies down and a 386 made one or two.
- **The 386 copy rate was never measured.** `486-dx2-66`'s `xcopy=0.05` is an
  estimate. By instruction count the loop is ~0.14 ms a KB on a 386DX-33 and
  ~0.3 on a 386SX-16. The Victory 286 copies at a measured 0.17 by LOADALL.
  So a 386 may have been no faster at this than the 286.
- **EMS vs XMS was a tier test**: EMS below a 386, XMS from one. That was
  written while a 386 always copied twice.

## 3. What was built

### A — XMEM.DRV's tier-2 copy by loaded bases (SPEC.md 41.15)

`apps/os88ub.inc`'s `ub_set` loads DS's and/or ES's hidden BASE at any linear
address. It sets CR0.PE, loads the register from a descriptor (base = the
address, limit 0FFFFh, access 93h) and clears PE. The register is then a
real-mode segment at that base. The next ordinary load restores it exactly,
so no widened limit is left behind. Each 2 KB chunk loads DS at the source
and ES at the destination, then moves with one `rep movsd`.

- Size: the image went 2,027 → 2,041 bytes, inside its 2 KB claim.
- `xm_arm` moved into the attach-only region, because attach is now its only
  caller.
- `ub_set`'s registers are chosen at assembly time (`UB_DS`/`UB_ES`). A
  run-time mask would have been 20 bytes of every image, and those 20 bytes
  are what kept XMEM.DRV inside its claim.

### B — a 386 decodes in place (SPEC.md 98.3.18.14)

`vp_ubld` stands where the 286 runs LOADALL, and `vp_xrst` ends either tier's
borrow. Everything else is the 286's design unchanged: the handles, the
`VP_DSDX`/`VP_DSEND` sites, the yields, the bounce and the mirror, and the
16-byte check.

- **Refusals**: the same list as the 286's, because none of those refusals is
  about the 286's instruction.
- **The speaker's clock stays refused on every tier.** The decode inside a
  borrow runs with interrupts off for up to `VP_XYB` record bytes, and one
  interrupt a sample cannot wait that long.
- **`[vp_noinp]` now refuses the hold as well as the bank.** That gives QEMU
  gates the copying path as an A/B.
- **Gate**: `tests/vidxip.py` now runs the SHIPPED in-place path on QEMU's
  386. It was the LA_SIM stand-in's job until now; `--sim` keeps that build
  assembling.
- **Breaking it on purpose works**: with `ub_set` taken out of `vp_ubld`, the
  16-byte check refuses and the play copies. That is the safe failure, and
  the row goes red.
- **Cost**: VIDEO.O88 +233 bytes.

### C — EMS against XMS by measured cost (SPEC.md 98.3.18.15)

`vp_epref` compares two prices:

- **EMS**: `[vp_er10]` − 10. The board's read time per KB in tenths of RAM's,
  less RAM's own.
- **XMS**: `[vp_xr10]` × copies. `[vp_xr10]` is a KB copied through
  `OSAPI_XMEM_COPY` in tenths of a RAM read, measured once an instance by
  `vp_xspeed`. Copies is 1 where the decode goes in place and 2 where it
  copies.

Two cases skip the comparison. A board within `VP_EFAST` of RAM wins without
the pool being timed. A board too slow to bank in (98.3.18.8) never wins.

**No emulator has an EMS board beside XMS**, so the choice itself is
unexercised. `vidxipxspeed` gates the measurement and that its block is given
back.

### D — the fill straight into XMS (SPEC.md 52.1.4, 98.3.18.15)

HDD.DRV publishes DSV_PKGCALL with two verbs, `HDV_ARM` and `HDV_DISARM`, and
no sector verb. While armed for one buffer, rung 1 sends any sector bound
for that buffer to the matching XMS address. It does this by loading ES's base
around the sector's `rep insw`, interrupts off for that sector only. The
player arms around each fill: the hold's, the copying bank's and the bank in
place's. It then copies up only what the DISARM map leaves clear.

- **Measured on QEMU** (`vidxiphd`): the in-place bank put 2,239 sectors
  straight into XMS and the copying bank 1,599. All 39 holds against the
  reference decode were right on both plays.
- **Breaking it on purpose works**: with the redirected sector's words read
  and dropped, the play stalls on garbage.
- **Bytes**: HDD.DRV used 416 of the 451 bytes its `align 512` was padding
  with. The image and the claim are 4,096 either way. VIDEO.O88 +598 bytes for
  C and D together.
- **286**: refused, not built. A LOADALL borrow (~150 us) costs more than the
  ~90 us copy up of the sector it replaces. LOADALL-PLAN 4.2's borrow across a
  run of sectors is the 286's answer, and it stays parked with L2.

### The encoder

`486-dx2-66` is `xplay="inplace"` now. Its `xbor_us=6.0` is ESTIMATED from the
instruction sequence. `xup` stays the unmeasured 0.05: on a rung-1 disk it is
zero, and an encode cannot know which disk will play it.

## 4. What is still unknown — M

Every 386 and 486 number above is an instruction count or QEMU's host. These
runs settle them:

1. **VIDDISK `X`** on the 486 laptop and the 386 laptop. It gives the tier-2
   copy per KB and per call, and whether 2 KB is the right chunk.
2. **VIDDISK's fill rows** on an IDE disk taken by rung 1, with and without
   the arm (a `VP_NOHARM` player is the A/B).
3. **A borrow's cost** on iron, for the encoder's `xbor_us`.
4. **A play** under `VPDIAG=1` on the 486, held and banked, card line
   `XMS in place (386)`.

The 486 laptop's 40 MB disk is slow and not period-correct. A play there
measures the CPU side honestly and the disk side pessimistically.

## 5. Not built, and why

- **Unreal FS/GS in the decoder.** It would need 32-bit addressing on every
  string instruction, and FS/GS belong to XMEM.DRV (41.4). The base load keeps
  the decoder byte-for-byte.
- **A flat DS kept across interrupts.** On a real 386 a real-mode load is
  believed to change only the base, so a flat DS with selector 0 would
  survive the ISRs. But any BIOS that returns from its own protected-mode
  trip with 64 KB limits turns that into a silent wrap or a #GP into IRQ5's
  vector. Not taken.
- **`rep insd`** (32-bit PIO). It helps only on VLB/PCI controllers that
  accept it, does nothing on ISA, and some VLB chips mishandle it. It needs a
  per-drive verify (read LBA 0 both ways, as `hd_twins` does) and a field
  machine with such a controller.
- **`rep movsd` / `rep stosd` in the decoder's long spans.** It needs a second
  copy of those lists. It helps a RAM target or a VLB/PCI card; an ISA VGA
  card is bus-bound (`rep movsw` 3.12 ms against `movsb`'s 3.30 on the 486).
  Worth taking after M if the shadow copy shows up.
- **A 486-scheduled decoder** (`mov`/`inc` instead of string ops). It would
  be a second decoder, for frames that already take 1/150 of a period.
- **E, more than 64 MB** (`E801h`, 32-bit KB). The owner deferred it as past
  the period.

## 6. Open items found on the way

- **A layered play, in place: FIXED 2026-10-17, no refusal** (SPEC.md
  98.3.18.14). The final pass found it: `vidlybank` went red once QEMU's 386
  went in place, and a refusal (`vp_lyipq`) shipped for a day while it was
  diagnosed. It was three defects, all on a 286 since 98.3.18.11/.13:
  `vp_lyown` stored its claim's segment as the layer's slot count (15072),
  a held file in place was never timed for the disk's verdict (its fill
  reads nothing; `vp_xfillt` reads and times it while the verdict is owed),
  and a bank in place was never timed either (no `vp_dtime` on the
  `vp_eread` arm, and two chunks where the test needs three).
  Gates: `vidlybank` (in place, asserted), `vidlybankcopy`,
  `vidlysplitinplace`, `vidlysplitinplacebase`. **What stays is capacity**:
  the split cannot resize a base bank that IS the ring, so in place each bank
  keeps the open's share (8 and 18 slots in `vidlysplit`'s clip, where copying
  gives 0 and 25). Resizing it would mean the split running before the ring is
  sized, and the split needs the CPU timed on a key that the ring reads.
- **The ring at THE GAP when G = 2048** (98.3.18.12) - FIXED 2026-10-17, the
  owner's call: `vp_kres` reserves the slack, so the ring is planned with it
  and a layout that loses a slot to it copies at the fast gap. What it was:
  `vidxms`, `vidxmsbios` and `vidxmsgap` failed on `origin/elendilon` too, so
  the defect was not this branch's. The ring is sized to every slot the free run holds and the 2G of
  slack is then claimed on top of it. G = 1024 needs 2 KB of slack, which the
  slot rounding usually leaves; G = 2048 needs 4 KB, which it often does not.
  The claim then falls back to an unplaced ring (`.rmx`'s retry), and the
  Victory 286 measures G = 2048. QEMU's G is host noise, so the two plain
  rows fail whenever it lands on 2048. Reserving the slack in the slot count
  costs a slot on some layouts. That trade is the owner's.
- **The CMOS index.** `ub_set` (like `xm_arm`) writes port 70h to mask NMI.
  The hook runs it from an ISR, so a CMOS read the hook interrupts between its
  `out 70h` and its `in 71h` would read register 0. The window is a few
  instructions and the kernel's clock reads go through int 1Ah. It is noted
  rather than fixed: the alternative, no NMI mask, risks a triple fault on
  any NMI that lands while PE is set.
- **SPEC.md 41.4's "a plain real-mode `mov fs` resets the limit"** is the
  stated reason for re-arming. On a 386 and later the hazard is a BIOS's own
  protected-mode excursion rather than a real-mode load. The rule stands
  either way; only its reason wants re-checking.
