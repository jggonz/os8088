; =============================================================================
; xmabi.inc - the private kernel <-> XMEM.DRV ABI (SPEC.md 41.12.2)
;
; INCLUDED BY BOTH SIDES, as source and never as a copy: kernel/xmem.inc (on
; kern_big) and drivers/xmem/xmem.asm - vxabi.inc's shape, in kernel/ for its
; reason: every tool that re-assembles kernel.asm already has kernel/ on its
; include path, and the driver's rule carries the one extra `-I kernel/`.
; Until kernel size pass 19 the two halves were typed out twice and only
; tests/unit/t_mirror.py held them together; SPEC.md 41.12.4 makes the whole
; subsystem silent BY DESIGN, so a drifted XMV_* was a wrong index into
; drv_call's service table and the kernel carrying on with no store.
;
; XM_ABI_VER is LOCKED to a fingerprint of this file by tools/abilock.py
; (SPEC.md 20.2.0.1): change a verb, a block field or a contract below and the
; lock asks whether the number moves.
; =============================================================================

XM_ABI_VER  equ 2               ; the private kernel<->overlay ABI, passed in
                                ; AH at DRVV_ATTACH; the driver refuses a
                                ; mismatch. A KERNEL.SYS and an XMEM.DRV of
                                ; different vintages - a half-copied floppy -
                                ; must fail loudly at load rather than quietly
                                ; at the first copy (SPEC.md 52.11's +10 word,
                                ; carried in a register because the image is
                                ; what compares it)

; --- the service table the overlay returns at attach -------------------------
XMV_CAPS    equ 0               ; out AX = free KB, DX:CX = pool base,
                                ;     BL = free block-table entries
XMV_ALLOC   equ 2               ; in DX:AX = bytes, BL = owner; out CF,
                                ;     DX:AX = base / AX = 0/1/2
XMV_FREE    equ 4               ; in DX:AX = base, BL = owner; out CF
XMV_COPY    equ 6               ; in ES:SI = an XMC_ block in KERNEL_SEG
XMV_RELINST equ 8               ; in AL = an instance slot being torn down
XMV_WALK    equ 10              ; in DX:DI = room for CX records; out AX
                                ; (ABI 2: osapi_xmem_walk on kern_big)
XMV_SIZE    equ 12

; --- the copy's parameter block, read through ES -----------------------------
; BOTH ENDS TRAVEL AS 32-BIT LINEAR ADDRESSES, and that is forced rather than
; chosen: slot OSAPI_XMEM_COPY takes the conventional end as ES:SI, and
; drv_call loads ES with KERNEL_SEG on its way into the image, so the
; caller's segment cannot survive the crossing. The kernel folds ES:SI into a
; linear address before it dispatches - arithmetic the resident xm_copy did
; internally anyway - and a block rather than registers because the ABI
; wants seven of them (two 32-bit ends, a count, a direction and an
; instance) and there are not seven to spare.
XMC_EXT     equ 0               ; dd: the extended end
XMC_CONV    equ 4               ; dd: the conventional end
XMC_LEN     equ 8               ; dw: bytes
XMC_DIR     equ 10              ; db: 0 = conventional -> extended, 1 = back
XMC_INST    equ 11              ; db: the requesting instance (the kernel
                                ; stamps it - snd_req_inst is kernel state)
XMC_SIZE    equ 12
