; tests/picomem/nehx.asm - ETHER.DRV's 8390 core, alone, for tests/picomem.py
; (SPEC.md 72.2). ne2000.inc is %included AS IT SHIPS; everything around it
; is the minimum it reads, so the row runs the driver's own probe, init,
; transmit and receive against a port model of the card under test.
cpu 8086
bits 16
org 0x100
%macro CLC_OR_STC 1
    db 0xA8
%1: stc
%endmacro
start:
    call ne_probe
    pushf
    pop ax
    mov [res_flags], ax
    call ne_init
    hlt
%include "ne2000.inc"
ep_di:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
res_flags:  dw 0
eth_base:   dw 0x300
eth_word:   db 0
eth_mac:    times 6 db 0
eth_pstart: db NE_PSTART
eth_pstop:  db NE_PSTOP
eth_next:   db NE_PSTART + 1
eth_txpg:   db NE_TXPAGE
eth_rxb:    times NE_FRAME + 4 db 0
eth_novw:   dw 0
eth_ntx:    dw 0
eth_rxh:    times 4 db 0
eth_txb:    times NE_FRAME db 0
ne_ring_read equ ne_ring_read_i
ne_tx        equ ne_tx_i
entry_tx:
    mov cx, 100
    call ne_tx
    hlt
entry_rx:
    call ne_rx
    pushf
    pop ax
    mov [res_flags], ax
    mov [res_len], cx
    hlt
res_len: dw 0
