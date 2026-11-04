# PCIETHER-PLAN — PCI network cards for kern_mega, starting with the RTL8139C

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
| D3 | stack shared as SOURCE through a NIC seam; `ETHER.DRV` byte-identical | lead | WEAVE-SPEC §1.2's rule; a copy of 8KB of TCP goes stale |
| D4 | PCI BIOS `int 1Ah` B1h with 16-bit registers, behind a 386+ CPU test | lead | keeps `cpu 8086` (§1); no 286 has PCI |
| D5 | polled, IMR = 0, one ring with WRAP, one TX buffer | lead | §72.2.1's reasoning holds; the stack is stop-and-wait |
| D6 | same class `DRVC_NET` as ETHER.DRV; the two are alternatives | lead | packages, DOS packet layer and The Wire unchanged |
| D7 | QEMU `-device rtl8139` is the gate; 86Box RTL8139C+ to LOOK | lead | §72.9; Bochs has no 8139, MartyPC no NIC |

## Stories

- [ ] **S1 Contract.** SPEC §125 and this plan. *(lead)*
- [b] **S2 NIC seam.** `drivers/ether/` reaches the card through the names
  §125.2 lists; `ETHER.DRV` assembles byte-identical. *(drv)*
- [b] **S3 PCI layer.** `drivers/pciether/pci.inc`: present, find by
  vendor:device table, config byte/word read/write, I/O BAR, enable I/O +
  bus master; the CPU gate. *(drv)*
- [b] **S4 RTL8139 backend.** `drivers/pciether/rtl8139.inc` per §125.4:
  reset, MAC, ring claim, receive, overflow, transmit, stop. *(drv)*
- [b] **S5 The driver.** `pciether.asm`, its build rule, the Ethernet page
  naming the card (`RTL8139`) and no I/O base field. *(drv)*
- [ ] **S6 The kern_mega row.** `drv_tab`, `SYSTEM.CFG` bit, title, Drivers
  page; second-of-class refusal; kern_mega budget held; other kernels
  byte-identical. *(kmega)*
- [ ] **S7 Disks.** On kern_mega's system disks only; a gate disk wanting
  it. *(harness)*
- [ ] **S8 QEMU gate.** A row that boots kern_mega with `-device rtl8139` on
  slirp and checks DHCP bound, a TCP fetch from the host, and the counters;
  registered in `tests/suite.py`. *(harness)*
- [ ] **S9 Period machine.** A `vm/` 86Box profile — a PCI board with the
  RTL8139C+ — booting the gate disk, and docs/86BOX.md's line. *(harness)*
- [ ] **S10 Next cards** *(deferred)*: RTL8029 (NE2000 on PCI: `ne2000.inc`
  behind the PCI layer's base), AMD PCnet, Intel 8255x.

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
