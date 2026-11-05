# PCIETHER-PLAN — PCI network cards for the big builds, starting with the RTL8139C

SPEC.md §125 is the contract. This is the story list and the record of how it
was built. Status: **[ ]** open, **[b]** built (host checks pass), **[x]** SEEN
working (a boot or a gate row said so, named beside it).

## The ask

> "can we add a driver for the Realtek RTL8139C network card?" — and, at the
> placement question: "PCIETHER.DRV so that we can re-use it later on as we
> add more drivers. Only kernel mega gets it."

## Decisions taken

| # | decision | by | why |
|---|---|---|---|
| D1 | separate `PCIETHER.DRV`, a family driver for PCI NICs | owner | later PCI cards are rows in it |
| D2 | kern_mega only: row, bit, file | owner | — |
| D2.1 | **superseded the same day**: every big build - kern_big, kern_emu, kern_mega - with the 360KB system disk leaving the file off | owner | "a releasable version of the OS with kern_big that has PCI support", on the condition that a machine without PCI is not affected: the row is unwanted by default and crosses no resident rung (72 bytes, `KERN_SIZE` unchanged), and kern_emu takes it because QEMU has an RTL8139 |
| D3 | stack shared as SOURCE through a NIC seam; `ETHER.DRV` byte-identical | lead | WEAVE-SPEC §1.2's rule; a copy of 8KB of TCP goes stale |
| D4 | PCI BIOS `int 1Ah` B1h with 16-bit registers, behind a 386+ CPU test | lead | keeps `cpu 8086` (§1); no 286 has PCI |
| D5 | polled, IMR = 0, one ring with WRAP, one TX buffer | lead | §72.2.1's reasoning holds; the stack is stop-and-wait |
| D6 | same class `DRVC_NET` as ETHER.DRV; the two are alternatives | lead | packages, DOS packet layer and The Wire unchanged |
| D7 | QEMU `-device rtl8139` is the gate; 86Box RTL8139C+ to LOOK | lead | §72.9; Bochs has no 8139, MartyPC no NIC |

## Stories

**[x] = seen** by the `pciether` row on 2026-11-04 (QEMU, kern_mega, a PCI
RTL8139 on slirp): PCIETHER.DRV attached at row 10 with its image above 1MB
(FC3000), QEMU's station address read from IDR0..5, DHCP bound to 10.0.2.15,
the Browser's GET reached the row's own server and the page laid out, rx 7
tx 9, no overflow reset.

- [x] **S1 Contract.** SPEC §125 and this plan. *(lead)*
- [x] **S2 NIC seam.** `drivers/ether/` reaches the card through the names
  §125.2 lists; `ETHER.DRV` assembles byte-identical (four variants and
  `os88net.raw` re-`cmp`ed by the verifier; the ETHER.DRV rows below all
  pass). *(drv)*
- [x] **S3 PCI layer.** `drivers/pciether/pci.inc`: present, find by
  vendor:device table, config byte/word read/write, I/O BAR, enable I/O +
  bus master; the CPU gate. *(drv)*
- [x] **S4 RTL8139 backend.** `drivers/pciether/rtl8139.inc` per §125.4:
  reset, MAC, ring claim, receive, transmit, stop. *Receive overflow and
  detach were not exercised by the row.* *(drv)*
- [b] **S5 The driver.** `pciether.asm`, its build rule, the Ethernet page
  naming the card (`RTL8139`) and no I/O base field. *Attached and served
  sockets (seen); the Ethernet page itself was not looked at.* *(drv)*
- [b] **S6 The kern_mega row.** `drv_tab` row 10, `SYSTEM.CFG` bit 11, title
  `PCI Ethernet`, `DRVE_CLASS` "Class already in use"; DRVM_PCIETH 39 KB and a
  per-kernel `DRVC_NET` ceiling (owner D3: `DRVM_CEIL_NET_MEGA` 39, 34
  elsewhere); kern_mega 5,120 spare, no rung crossed; kern_big, kern_emu,
  kern_small byte-identical. *The row attaching is seen; the Drivers page,
  the second-of-class refusal and DOS's "Up to 39K" were not looked at.*
  *(kmega)*
- [x] **S7 Disks.** On kern_mega's system disks only; `make pciethertest`.
  *(harness)*
- [x] **S8 QEMU gate.** `tests/pciether.py`, soak row `pciether`, and
  `stknosave-pciether` for the shared stack's NOSAVE markers. *(harness)*
- [b] **S9 Period machine.** `vm/pentium-pciether` (430FX, `rtl8139c+` -
  the internal name at 86Box's pinned commit) and `make pentium-pciether`.
  *Never launched.* *(harness)*
- [ ] **S10 Next cards** *(deferred)*: RTL8029 (NE2000 on PCI: `ne2000.inc`
  behind the PCI layer's base), AMD PCnet, Intel 8255x.

## Open findings

1. **kern_mega loaded SOUND.DRV against SYSTEM.CFG.** On QEMU with an AdLib,
   the gate disk (bitmap 0x0800, bit 11 alone) came up with row 0 SOUND.DRV
   `WANT 1, ERR 0`, against SPEC §51.3.1's "a settings file that
   deserialises always wins". Not this feature's change (row 10 is appended
   after every existing one); probably older kern_mega behaviour, unproven.
   The gate now runs `NOCARD=1`. Needs its own look.
2. **A 3.6-4KB frame at the ring's end** (WRAP=1, a jumbo or hostile frame)
   can write up to ~512 bytes past the claim; Linux has the same exposure.
   Closing it costs ~1KB of claim - an owner decision.
3. **DOS trips with a live bus master.** Restart unloads every driver; nobody
   has confirmed that kern_mega's DOS-trip and windowed-DOS paths never hand
   the ring's conventional memory to DOS while the driver stays loaded.
4. **No ring memory reads as "No hardware found"** (`eth_attach`'s `.none`).
5. **RXOVW resets the whole ring** (§72.2's 8390 rule); Linux only acks it.
6. **Rows not run:** `megacdrom` (its disk's `KMODDIR` changed),
   `pmbox`, `pmbox2` - they need the automation 86Box (`build/86box/`), which
   this machine has not built.

## Log
- 2026-11-04 (drv): S2-S5 built, host checks only. ETHER.DRV (plain, -DETH_BASE,
  -DETHPROF) and OS88NET.COM `cmp` clean across the NIC seam; `ethdrv.inc` is
  the old ether.asm body, `nicne2k.inc` the 8390's seam. `pciether.drv`:
  17,153 bytes unpacked, 10,859 packed, claim 11,280 bytes at attach. Nothing
  has been loaded in an emulator yet. The 32-bit registers are dword accesses
  (66h prefix), a correction to the first draft of 125.4 - see there.
- 2026-11-04 (verify-drv, Opus): ETHER.DRV (plain, -DETH_BASE, -DETHPROF,
  -DETH_IP) and os88net.raw re-`cmp`ed clean against `pciether` from a private
  tree. Fixed: the ring was reached as `mov es, [rt_seg]`, a paragraph from a
  boot attach in real mode that is a #GP once kern_mega is in protected mode
  (now `rt_es`, SEG_WIN over the paragraph, EMS.DRV's shape); the image is
  `OS88_DRV_HIGHOK` after all (the stated reason against it did not hold, and
  `OS88_DRV_HICLAIM` alone sets no header bit) with the ring `MEMP_CONV`;
  `pci_present` keeps DI (B101h returns EDI); an `FFF0h` header waits, and the
  length bound is Linux's 1,536 + 4. Image 17,259 unpacked, 10,950 packed.
- 2026-11-04 (lead, final pass): `pciether` failed twice before passing.
  First `dispcp`'s listing read the Disk window's names through `dsk_dseg`
  as a paragraph - under kern_mega it is a selector - so `os88qemu.seg_snap`
  now resolves through `q.lin` when the QEMU object decodes selectors
  (`MQemu`, nothing else). Then SOUND.DRV was up beside it (finding 1); the
  gate's machine is now `NOCARD=1`. Union: 15 rows + `pciether` +
  `stknosave-pciether` pass on the host, `megainst`, `megahddtick`, `brmega`
  pass under Bochs in Docker.
- 2026-11-04 (kern_big, D2.1): the row is `%ifdef DRV_PCIETH`, defined
  wherever `KERN_BIG` is, so kern_emu has it too (row 11, `DRV_MAX` 12, and
  `DQ_MAXREC` 11 -> 12 in the kernel and both SDKs; DRVC_NET is never
  suspended, so no older buffer gets a twelfth record). kern_big: `.text`
  +29, `.cold` +43, boot blob +14, no rung crossed, `KERN_SIZE` 96,768
  unchanged. The blob was 2 bytes from its 13-sector ceiling, so the title
  became `'PCI '` in front of `'Ethernet'` (sharing nine bytes) and
  `ovl_spl_msg_drv`'s `call`/`ret` stub became the body itself (four):
  6,655 of 6,656. `DRVM_CEIL_NET_MEGA` is `DRVM_CEIL_NET_PCI` and every
  `DOS.O88` quotes it. `rt_outd`/`rt_ind` now hold `cli` across EAX's top
  half (a real-mode `sch_switch` saves 16-bit registers only). The file is
  on every big system disk but the 360KB one. Gate: `make pcibigtest`,
  `tests/pciether.py --big`, soak row `pcibig`.
