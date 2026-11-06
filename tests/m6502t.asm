; =============================================================================
; os8088 - tests/m6502t.asm - apps/arcade/arc6502.inc on its own, for
; tests/m6502.py to run under Unicorn against tools/qbsnd.py's CPU6502
;
;     nasm -f bin -w+error -I apps/arcade/ -I tests/ tests/m6502t.asm -o TMP
;
; Not shipped software. The image is loaded at offset 0 of a segment that is
; CS = DS (the package's shape); the 6502's memory is another segment, named
; in m6_seg. The harness finds everything through the HEADER at offset 0 - a
; signature and a fixed list of offsets - so it needs no map or listing.
;
; THE HOOKS are deliberately thin: the harness puts a Unicorn code hook on
; the first instruction of each, sees BX (and AL), and for a read stores the
; byte it wants answered into m6t_ioval before the `mov` runs. The 8086 code
; between the hook and the core is still the real thing: it must preserve
; what the contract says, and the harness checks the core's registers.
; =============================================================================
    cpu 8086
    bits 16
    org 0

m6t_hdr:
    db 'M6T1'                       ; +0  the signature
    dw m6t_reset                    ; +4  call m6_reset, stop at m6t_stop
    dw m6t_run                      ; +6  call m6_run (AX = budget), stop
    dw m6t_stop                     ; +8  where both stop
    dw m6_ior                       ; +10 the read hook
    dw m6_iow                       ; +12 the write hook
    dw m6t_ioval                    ; +14 the byte m6_ior answers
    dw m6_a, m6_x, m6_y, m6_s, m6_p ; +16 the state, in this order
    dw m6_pc, m6_cyc, m6_seg, m6_irq, m6_debt, m6_bad
    dw m6t_core0, m6t_core1         ; +38 the core's extent (for counting)

m6t_reset:
    call m6_reset
    jmp short m6t_stop
m6t_run:
    call m6_run
m6t_stop:
    hlt

m6_ior:
    mov al, [m6t_ioval]
    ret

m6_iow:
    ret

m6t_core0:
%include "arc6502.inc"
m6t_core1:

; the state arc6502.inc asks its includer for
m6_a:       db 0
m6_x:       db 0
m6_y:       db 0
m6_s:       db 0
m6_p:       db 0
m6_irq:     db 0
m6t_ioval:  db 0
            db 0
m6_pc:      dw 0
m6_seg:     dw 0
m6_debt:    dw 0
m6_bad:     dw 0
m6_tgt:     dw 0
m6_left:    dw 0
m6_cyc:     dd 0
m6_c0:      dd 0
