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

; -----------------------------------------------------------------------------
; pce_find - probe the bus, take the first card pce_tab knows, load its
; vectors and let it claim what it needs (DRVV_ATTACH's half)
; out: CF=0 and [eth_base] = its I/O base; CF=1 = no card, or no memory for it
; -----------------------------------------------------------------------------
pce_find:
    push ax
    push bx
    push cx
    push si
    push di
    call pci_probe              ; BX = bus/devfn, AX = base, SI = the row
    jc  .out
    mov [eth_base], ax
    mov [pce_bdf], bx
    mov ax, [si+4]
    mov [pce_name], ax
    mov si, [si+6]              ; the backend's vectors...
    mov di, pce_vt
    mov cx, 5
.cp:
    mov ax, [si]
    mov [di], ax
    add si, 2
    add di, 2
    loop .cp
    call [pce_vt + PCEV_CLAIM]  ; ...and its memory
    jnc .out
    mov word [eth_base], 0
    stc
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

