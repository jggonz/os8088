; =============================================================================
; os8088 - pcifamily.inc: PCIETHER.DRV's NIC backend (SPEC.md 125.2)
;
; Included in place by ethdrv.inc as NIC_BACKEND, where ne2000.inc is in
; ETHER.DRV: the PCI layer, the dispatch the stack's nic_* names land on, the
; probe, and the cards.
; =============================================================================
%include "pci.inc"
%include "rtl8139.inc"

; --- the dispatch: one jmp through the chosen backend's vector ---------------
pce_init:   jmp [pce_vt + PCEV_INIT]
pce_rx:     jmp [pce_vt + PCEV_RX]
pce_tx:     jmp [pce_vt + PCEV_TX]
pce_stop:   jmp [pce_vt + PCEV_STOP]
pce_halt:   jmp [pce_vt + PCEV_HALT]

; -----------------------------------------------------------------------------
; pce_find - probe the bus, take the first card pce_tab knows, load its
; vectors and let it claim what it needs (DRVV_ATTACH's half)
; out: CF=0 and [eth_base] = its I/O base; CF=1 = no card, or no memory for it
; clobbers: AX, BX, CX, DI - eth_attach, NIC_ATTACH_FIND's one caller, banks
; all four. SI is kept (a refused attach hands the kernel its own back) and
; DX is too: the attach's caller holds it
;
; A REFUSED CLAIM LEAVES [eth_base] SET, and that is the attach's own rule
; (ethdrv.inc, .none): a refused attach is handed straight back to the heap,
; so nothing reads the image's state again
; -----------------------------------------------------------------------------
pce_find:
    push si
    call pci_probe              ; BX = bus/devfn, AX = base, SI = the row
    jc  .out
    mov [eth_base], ax
    mov ax, [si+4]
    mov [pce_name], ax
    mov si, [si+6]              ; the backend's vectors...
    xor bx, bx
.cp:
    mov ax, [si+bx]
    mov [pce_vt+bx], ax
    inc bx
    inc bx
    cmp bl, PCEV_HALT + 2
    jb  .cp
    call [pce_vt + PCEV_CLAIM]  ; ...and its memory
.out:
    pop si
    ret
