; tests/csprobe/feharness.asm - REDLINE's chipset WRITE side (csfe.inc,
; csapply.inc) on its own, for tests/csprobe.py. Segment 0, org 100h. The
; test parks IP on a routine with h_ret's address pushed, so every call ends
; on the hlt there. od_read/od_write are stubs over h_file, which the test
; fills; OSAPI_CPU_INFO answers [h_tier].
cpu 8086
bits 16
org 0x100
CPU_8086 equ 0
CPU_286  equ 1
CPU_386  equ 2
FERR_NOENT equ 4
%define OSAPI_CPU_INFO h_cpuinfo
%define CS_LOADER                   ; csapply.inc's load and apply, RLAPPLY's
h_ret:
    hlt
h_tier:     db 1
h_flen:     dw 0                    ; 0 = no file
h_file:     times 64 db 0
h_wlen:     dw 0
h_cpuinfo:
    mov al, [h_tier]
    ret
od_read:                            ; ES:BX = buffer, CX = capacity
    mov ax, [h_flen]
    or ax, ax
    jz .none
    cmp ax, cx
    ja .big
    push cx
    push si
    push di
    mov cx, ax
    mov si, h_file
    mov di, bx
    cld
    rep movsb
    pop di
    pop si
    pop cx
    xor dx, dx
    clc
    ret
.big:
    mov ax, 5
    stc
    ret
.none:
    mov ax, FERR_NOENT
    stc
    ret
od_write:                           ; ES:BX = bytes, CX = count
    push si
    push di
    push cx
    mov [h_wlen], cx
    mov si, bx
    mov di, h_file
    cld
    rep movsb
    pop cx
    pop di
    pop si
    xor ax, ax
    clc
    ret
h_home:     db 12                   ; the FE3600's home: the model's R4
h_sweep:                            ; the FE3600's sweep, the test's ROM at
    xor bx, bx                      ; C000 and no PicoMEM buffer
    jmp short h_sweep_fe
h_sweep_pm:                         ; ...and with a PicoMEM's buffer at D000
    mov bx, 0xD000
h_sweep_fe:
    mov ax, 0xA000
    mov cx, 0xC000
    call cs_sw_refs
    mov si, cs_klist_fe             ; REDLINE's own path: the row's list
    mov al, [h_home]
    jmp cs_sw_tune
h_tune_ne:                          ; every NEAT knob, as REDLINE's Tune
    mov ax, 0xA000
    xor bx, bx
    mov cx, 0xC000
    cmp byte [h_norom], 0           ; a board with shadowing ON: no ROM to
    je .refs                        ; read, no PicoMEM, so nothing 8-bit
    xor cx, cx                      ; MEMORY at all
.refs:
    call cs_sw_refs
    mov si, cs_klist_ne
    jmp cs_sw_tune
h_sweep_ne:                         ; the NEAT's, the same references; home
    mov ax, 0xA000                  ; is what the chip says it runs
    xor bx, bx
    mov cx, 0xC000
    call cs_sw_refs
    call cs_ne_get
    mov bx, cs_desc_ne
    jmp cs_sw_sweep
h_flat:     db 0                    ; 1: every step times the same
h_dmabad:   db 0                    ; 1: the DMA read comes back wrong
cs_sw_dma:                          ; REDLINE re-reads A:'s boot sector; the
    push ax                         ; harness answers h_dmabad (CF = bit 0)
    mov al, [h_dmabad]
    shr al, 1
    pop ax
    ret
h_bus_ne:                           ; Bus clock x2, as cs_do_bus runs it:
    mov ax, 0xA000                  ; the references, every wait and delay
    xor bx, bx                      ; to its longest, then the experiment's
    mov cx, 0xC000                  ; list with its knobs switched on
    call cs_sw_refs
    mov si, cs_klist_nex + 2
.l:
    mov bx, [si]
    or bx, bx
    jz .run
    mov [cs_sw_desc], bx
    mov al, [bx + CSD_BOOK]
    call cs_sw_set
    add si, 2
    jmp short .l
.run:
    mov byte [cs_sw_xmode], 1
    mov si, cs_klist_nex
    call cs_sw_tune
    mov byte [cs_sw_xmode], 0
    ret
h_norom:    db 0                    ; 1: h_tune_ne has no ROM reference
cs_sw_measure:                      ; REDLINE times each step; the model has
    push ax                         ; no clock, so the harness makes one up:
    push bx                         ; 300 + 100 x the candidate, a step one
    push cx                         ; bus clock FASTER on all four - or 500
    mov ax, 500                     ; everywhere with h_flat, a step that
    cmp byte [h_flat], 0            ; buys nothing
    jne .put
    mov al, [cs_sw_cand]
    mov ah, 100
    mul ah
    add ax, 300
.put:
    xor bx, bx
    mov cx, 4
.w:
    mov [cs_sw_ns + bx], ax
    add bx, 2
    loop .w
    pop cx
    pop bx
    pop ax
    ret
%include "redline/csrows.inc"
%include "redline/csfe.inc"
%include "redline/csne.inc"
%include "redline/csapply.inc"
%include "redline/csweep.inc"
cs_sw_ref:  times 1024 db 0
