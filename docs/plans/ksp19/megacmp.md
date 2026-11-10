# Kernel size pass 19 - `megacmp`: kern_mega's placement and compaction audit

Branch `ksp19-megacmp`, cut from `kernel-size-19` at `7ba85e9f`. The owner's
concern: *"This seems like a yoyo; we JUST did a bunch of work to get mega able
to fully compact. Make sure we didn't break that work."*

## THE FLAG (for `kbpara`, written first)

**`MEMP_DMAPOOL equ 0x4000`** - a bit of **CX** on an `OSAPI_MEM_P` claim
verb (`MEMP_CLAIM` / `MEMP_CLAIM_HI`, SPEC.md 50.3.7), beside the head:

    mov ax, <paragraphs>
    mov cx, <head paragraphs> | MEMP_DMAPOOL
    mov bl, MEMP_CLAIM          ; or MEMP_CLAIM_HI
    call OSAPI_MEM_P

* **On kern_mega in protected mode, for an OS88_HIGHMEM package** (never a
  driver), a claim of 64KB or less with a head of 64KB or less is a **pool
  block above 1MB, BORN PINNED, its head inside one 64KB physical page**
  (`mega_claimdf`, `MX_FDMA | MX_FPIN`, KERN-MEGA-PLAN 14.160) - exactly what
  the withdrawn KB DMA doors (`OSAPI_MEM_CLAIM_DMA` / `_DMA_HI`) gave such a
  package with CX != 0. `OSAPI_MEM_MOVABLE` on its selector unpins it (or
  pins it again), and both pool compactions keep the head page-safe
  (`mx_dmafit`); a regrow keeps the head (`mega_claimregrowf`). The pool
  counts in KB: the claim and the head are each rounded UP to whole KB there,
  and AX comes back as the paragraphs of the KB granted.
* **Anywhere else** - every other kernel, real mode, a driver, a package
  without OS88_HIGHMEM, a claim or head past 64KB, no pool room - the bit is
  ignored: the claim is the paragraph door's ordinary head claim,
  conventional and page-safe, as without it.
* **The head with the bit off is conventional** (the paragraph door's contract
  since it was made: a page-safe buffer a real-mode path names). Head 0 with
  the bit is no head and conventional, as `MEMP_CONV`. Never set both.
* The **KB arm** (`MEMP_KB + MEMP_KBH`, i.e. `MEMKB_DMA` / `MEMKB_DMA_HI`)
  sets it for you: a KB head is a pool head where it can be, as the doors.
  **Converting a `MEMKB_DMA*` site to paragraphs: OR `MEMP_DMAPOOL` into CX**
  - every one of them was a KB DMA door at the base. The one KB-arm site that
  must stay conventional is Video's PKG_MEGA `vp_rclaim` `.p` (the real-mode
  bracket reads the ring), and it is the paragraph door without the bit here.

Costs and the audit follow below once measured.
