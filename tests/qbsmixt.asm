; =============================================================================
; os8088 - tests/qbsmixt.asm - Q*bert's sound board as ARCADE runs it
; (apps/arcade/arcqbs.inc over apps/arcade/arc6502.inc), on its own, for
; tests/qbsmix.py to run under Unicorn against tools/qbsnd.py
;
;     nasm -f bin -w+error -I apps/arcade/ -I tests/ tests/qbsmixt.asm -o TMP
;
; Not shipped software. The image sits at offset 0 of a segment that is CS =
; DS; the harness lays out the rest of the machine (the .ROM's claim, the
; game's segment, the 6503's memory, the stream's ring) and finds everything
; through the HEADER at offset 0. The three OS calls arcqbs.inc makes are
; stubs here: a 286 (CPU_INFO), and a claim at a segment the harness names.
; =============================================================================
    cpu 8086
    bits 16
    org 0

CPU_286     equ 1
CPU_386     equ 2
%define OSAPI_CPU_INFO  qt_cpu
%define OSAPI_MEM_CLAIM qt_claim
%define OSAPI_MEM_FREE  qt_free

qt_hdr:
    db 'QBS1'                       ; +0  the signature
    dw qt_prep                      ; +4  qbs_prep, then stop (CF in qt_cf)
    dw qt_tick                      ; +6  qbs_tick
    dw qt_mix                       ; +8  qbs_mix: CX samples to ES:DI
    dw qt_stop                      ; +10 where all three stop
    dw ar_rimgc, ar_rimg, qb_gseg   ; +12 the cells the harness fills
    dw qt_seg                       ; +18 the segment a claim answers
    dw qt_cf                        ; +20 qbs_prep's CF
    dw m6_cyc, qbs_dac, m6_bad      ; +22

qt_prep:
    call qbs_prep
    mov byte [qt_cf], 0
    jnc qt_stop
    mov byte [qt_cf], 1
    jmp short qt_stop
qt_tick:
    call qbs_tick
    jmp short qt_stop
qt_mix:
    call qbs_mix
qt_stop:
    hlt

qt_cpu:
    mov al, CPU_386
    ret
qt_claim:
    mov dx, [qt_seg]
    clc
    ret
qt_free:
    ret

%include "arc6502.inc"
%include "arcqbs.inc"

; the cells arcqbs.inc and arc6502.inc ask the package for
qt_seg:     dw 0
qt_cf:      db 0
ar_rimgc:   dw 0
ar_rimg:    dw 0
qb_gseg:    dw 0
m6_a:       db 0
m6_x:       db 0
m6_y:       db 0
m6_s:       db 0
m6_p:       db 0
m6_irq:     db 0
m6_pc:      dw 0
m6_seg:     dw 0
m6_debt:    dw 0
m6_bad:     dw 0
m6_tgt:     dw 0
m6_left:    dw 0
m6_cyc:     dd 0
m6_c0:      dd 0
qbs_on:     dw 0
qbs_seg:    dw 0
qbs_left:   dd 0
qbs_frac:   dw 0
qbs_dac:    db 0
qr_state:
qr_pa:      db 0
qr_pb:      db 0
qr_ddra:    db 0
qr_ora:     db 0
qr_ddrb:    db 0
qr_orb:     db 0
qr_tval:    db 0
qr_tsh:     db 0
qr_tie:     db 0
qr_tflag:   db 0
qr_pa7f:    db 0
qr_pa7ie:   db 0
qr_pa7pos:  db 0
qr_tat:     dd 0
QR_SIZE     equ $ - qr_state
qr_sah:     db 0
