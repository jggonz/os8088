; =============================================================================
; os8088 - apps/runcpm/hosttest/rcz80test.asm
;
; Part of RUNCPM (SPEC.md 71), a reimplementation of RunCPM 6.9 by Marcelo
; Dantas / "Mockba the Borg" (MIT licence, Copyright (c) 2017 Mockba the
; Borg): the CP/M it stands up around the core is cpm.h's _PatchCPM in
; miniature.
;
; A BOOT SECTOR THAT RUNS THE Z80 CORE ON A REAL x86, with nothing else
; running - the fast gate for apps/runcpm/rcz80.inc and the way it was
; developed: assemble, boot in QEMU, read the serial port. The core is
; %included, not copied, so what runs is the shipping text (LESSONS.md 4:
; the cwmovetest precedent), under the one condition that makes this OS
; different from every other target it could be tested on: SS != DS. Here CS
; = DS = 0 stand in for the package's segment (the core reaches its statics
; through CS and rcmem.inc through DS, and in a package the two are equal),
; SS = 0x1000 is the task stack, ES = 0x3000 stands in for KERNEL_SEG and is
; checked after every call, and the Z80's 64KB is the segment 0x4000.
;
; WHAT IT RUNS: the .COM file rcz80test.sh appends to the image (ZEXDOC.COM
; off the master disk by default - the Z80 instruction exerciser, documented
; flags, which prints one line per instruction group and 'Tests complete'),
; loaded at 0x0100 the way the BDOS loads a transient, with the smallest
; CP/M that lets it speak: page zero's two jumps, RunCPM's RST 08h/RST 10h
; handoff pages at 0xFE00/0xEC00 (cpm.h _PatchCPM), BDOS 2 and 9 and BIOS
; CONOUT answered on the serial port, and BDOS 0 / BIOS BOOT/WBOOT ending
; the run. Everything else the program could ask for is answered with A = 0
; and printed as a '?' - ZEXDOC asks for nothing else.
;
; It exits QEMU through the isa-debug-exit device (port 0xF4) so the script
; can wait for it: exit code 1 = the program ended, 3 = HALT, 5 = the run
; hit a trap it does not model.
;
; RUN IT:  apps/runcpm/hosttest/rcz80test.sh [file.com]
; =============================================================================

cpu 8086
bits 16
org 0x7C00

SEG_STACK   equ 0x1000
SEG_KERNEL  equ 0x3000              ; "KERNEL_SEG": must come back intact
SEG_Z80     equ 0x4000

RUN_N       equ 20000               ; control transfers per rc_run
IMG_SECTORS equ 64                  ; 32KB read; the script pads to 1.44MB

; `push imm` is not an 8086 instruction (CLAUDE.md), so:
%macro PUSHI 1
    mov ax, %1
    push ax
%endmacro

; the layout the core expects (crt0.asm's): its tables in .rodata after the
; code, its statics in .bss after that - nobits, past the loaded image, in RAM
section .text
section .rodata follows=.text
section .data   follows=.rodata
section .bss    follows=.data nobits
section .text

; -----------------------------------------------------------------------------
; STAGE 1 - the boot sector: load the rest of the image and jump to it
; -----------------------------------------------------------------------------
start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7000
    sti
    cld
    ; read sectors 1..IMG_SECTORS-1 with a CHS walk (18 sectors a track, 2
    ; heads), one sector at a time
    mov word [lba], 1
    mov bx, 0x7E00
.rd:
    mov ax, [lba]
    cmp ax, IMG_SECTORS
    jae .go
    xor dx, dx
    mov cx, 18
    div cx                          ; ax = track*2+head, dx = sector-1
    mov cl, dl
    inc cl                          ; sector
    mov dh, al
    and dh, 1                       ; head
    shr ax, 1
    mov ch, al                      ; cylinder
    mov dl, 0
    mov ax, 0x0201
    int 0x13
    jc .err
    add bx, 512
    inc word [lba]
    jmp .rd
.go:
    jmp 0x0000:body
.err:
    mov al, 'D'
    call putc
    mov al, 7
    jmp exit
lba: dw 0

    times 510-($-$$) db 0
    dw 0xAA55

; -----------------------------------------------------------------------------
; STAGE 2
; -----------------------------------------------------------------------------
body:
    cli
    mov ax, SEG_STACK
    mov ss, ax
    mov sp, 0xFFF0
    xor ax, ax
    mov ds, ax                      ; DS = CS = 0: "the package"
    mov ax, SEG_KERNEL
    mov es, ax
    sti
    cld

    mov si, msg_hello
    call puts

    ; --- the CP/M image: page zero, the handoff pages, the program ------------
    mov word [_rc_z+RZ_SEG], SEG_Z80
    PUSHI 0x8000                    ; rc_zfill(0, 0, 0x8000)
    PUSHI 0
    PUSHI 0
    call _rc_zfill
    add sp, 6
    PUSHI 0x8000                    ; rc_zfill(0x8000, 0, 0x8000)
    PUSHI 0
    PUSHI 0x8000
    call _rc_zfill
    add sp, 6

    ; 0000: JP FE03 (warm boot)  0005: JP EC06 (BDOS)
    PUSHI 3
    PUSHI page0
    PUSHI 0
    call _rc_zcopy_in
    add sp, 6
    PUSHI 3
    PUSHI page5
    PUSHI 5
    call _rc_zcopy_in
    add sp, 6
    ; BDOS: EC06: JP ED00; ED00: RST 10h; RET
    PUSHI 3
    PUSHI bdosjmp
    PUSHI 0xEC06
    call _rc_zcopy_in
    add sp, 6
    PUSHI 2
    PUSHI bdospage
    PUSHI 0xED00
    call _rc_zcopy_in
    add sp, 6
    ; BIOS: FE00+3n: JP FF00+3n; FF00+3n: RST 08h; RET; NOP  (17 entries)
    mov cx, 17
    mov bx, 0xFE00
.bios:
    push cx
    push bx
    mov ax, bx
    add ax, 0x100
    mov [biosjmp+1], ax
    PUSHI 3
    PUSHI biosjmp
    push bx
    call _rc_zcopy_in
    add sp, 6
    pop bx
    push bx
    add bx, 0x100
    PUSHI 3
    PUSHI biospage
    push bx
    call _rc_zcopy_in
    add sp, 6
    pop bx
    add bx, 3
    pop cx
    loop .bios
    ; the program at 0100
    PUSHI zprog_len
    PUSHI zprog
    PUSHI 0x0100
    call _rc_zcopy_in
    add sp, 6
    ; the stack: SP = E3FE with 0000 on top, so a RET warm-boots
    PUSHI 0
    PUSHI 0xE3FE
    call _rc_wr16
    add sp, 4

    ; --- the registers ---------------------------------------------------------
    mov word [_rc_z+RZ_AF], 0
    mov word [_rc_z+RZ_BC], 0
    mov word [_rc_z+RZ_DE], 0
    mov word [_rc_z+RZ_HL], 0
    mov word [_rc_z+RZ_PC], 0x0100
    mov word [_rc_z+RZ_SP], 0xE3FE
    mov word [_rc_z+RZ_IX], 0
    mov word [_rc_z+RZ_IY], 0
    mov word [_rc_z+RZ_IFF], 0
    mov word [_rc_z+RZ_IM], 0

; --- the run loop ------------------------------------------------------------
run:
%ifdef TRACE                        ; -DTRACE: one control transfer a call,
    PUSHI 1                         ; and PC printed after each, TRACE of them
    call _rc_run
    add sp, 2
    push ax
    dec word [trace_left]
    jz .tdone
    mov ax, [_rc_z+RZ_PC]
    call puthex
    mov al, '/'
    call putc
    mov ax, [_rc_z+RZ_SP]
    call puthex
    mov al, '/'
    call putc
    mov ax, [_rc_z+RZ_AF]
    call puthex
    mov al, '/'
    call putc
    mov ax, [_rc_z+RZ_HL]
    call puthex
    mov al, ' '
    call putc
    pop ax
    jmp short .traced
.tdone:
    mov al, 6
    jmp exit
.traced:
%else
    PUSHI RUN_N
    call _rc_run
    add sp, 2
%endif
    ; the discipline: ES back, DF clear
    push ax
    mov bx, es
    cmp bx, SEG_KERNEL
    jne .badreg
    pushf
    pop bx
    test bx, 0x0400
    jnz .badreg
    pop ax
    cmp ax, RC_RUN_SLICE
    je run
    cmp ax, RC_RUN_BDOS
    je bdos
    cmp ax, RC_RUN_BIOS
    je bios
    ; HALT
    mov si, msg_halt
    call puts
    mov al, 3
    jmp exit
.badreg:
    mov si, msg_badreg
    call puts
    mov al, 5
    jmp exit

bios:
    mov ax, [_rc_z+RZ_PC]
    dec ax                          ; the RST's own address: FF00 + 3n
    and ax, 0xFF
    cmp al, 0                       ; BOOT
    je done
    cmp al, 3                       ; WBOOT
    je done
    cmp al, 12                      ; CONOUT: C
    jne .other
    mov al, [_rc_z+RZ_BC]
    call putc
    jmp run
.other:
    mov al, '?'
    call putc
    jmp run

bdos:
    mov al, [_rc_z+RZ_BC]           ; C = the function
    cmp al, 0
    je done
    cmp al, 2
    je .conout
    cmp al, 9
    je .str
    ; anything else: A = 0, HL = 0, and a mark
    mov al, '?'
    call putc
    jmp .ret0
.conout:
    mov al, [_rc_z+RZ_DE]
    call putc
    jmp .ret0
.str:
    mov bx, [_rc_z+RZ_DE]
.s:
    push bx
    call _rc_rd
    add sp, 2
    cmp al, '$'
    je .ret0
    call putc
    inc bx
    jmp .s
.ret0:
    ; the BDOS's own tail (cpm.h _Bdos): HL = 0, C = E, B = H, A = L
    mov word [_rc_z+RZ_HL], 0
    mov al, [_rc_z+RZ_DE]
    mov [_rc_z+RZ_BC], al
    mov byte [_rc_z+RZ_BC+1], 0
    mov byte [_rc_z+RZ_AF], 0       ; A (the low byte of AF) = L = 0
    jmp run

done:
    mov si, msg_done
    call puts
    mov al, 0                       ; exit code 1
    jmp exit

; -----------------------------------------------------------------------------
; the routines under test - THE SHIPPING TEXT
; -----------------------------------------------------------------------------
%include "rcz80.inc"
%include "rcmem.inc"

section .text
; -----------------------------------------------------------------------------
; serial output (COM1 -> QEMU's stdout), and the exit
; -----------------------------------------------------------------------------
putc:
    push ax
    push dx
.wait:
    mov dx, 0x3FD
    in al, dx
    test al, 0x20
    jz .wait
    pop dx
    pop ax
    push dx
    mov dx, 0x3F8
    out dx, al
    pop dx
    ret

puts:
    push ax
.l:
    mov al, [cs:si]
    inc si
    or al, al
    jz .e
    call putc
    jmp .l
.e:
    pop ax
    ret

puthex:                             ; AX in hex
    push cx
    mov cx, 4
.h: rol ax, 1
    rol ax, 1
    rol ax, 1
    rol ax, 1
    push ax
    and al, 15
    cmp al, 10
    jb .d
    add al, 7
.d: add al, '0'
    call putc
    pop ax
    loop .h
    pop cx
    ret
%ifdef TRACE
trace_left: dw TRACE
%endif

exit:                               ; AL -> isa-debug-exit: code = AL*2+1
    mov dx, 0xF4
    out dx, al
    cli
    hlt
    jmp exit

chk_es:    dw 0
page0:     db 0xC3, 0x03, 0xFE
page5:     db 0xC3, 0x06, 0xEC
bdosjmp:   db 0xC3, 0x00, 0xED
bdospage:  db 0xD7, 0xC9
biosjmp:   db 0xC3, 0x00, 0x00
biospage:  db 0xCF, 0xC9, 0x00

msg_hello:  db 'rcz80test: the Z80 core in raw QEMU, SS != DS', 13, 10, 0
msg_done:   db 13, 10, 'rcz80test: program ended (BOOT/WBOOT/BDOS 0)', 13, 10, 0
msg_halt:   db 13, 10, 'rcz80test: HALT', 13, 10, 0
msg_badreg: db 13, 10, 'rcz80test: FAIL - ES or DF not restored', 13, 10, 0

; the program: whatever the script appended, padded to a sector
zprog:
%ifdef ZPROG
    incbin ZPROG
%else
    ; a stand-in: LD C,9 / LD DE,msg / CALL 5 / RET
    db 0x0E, 0x09, 0x11, 0x09, 0x01, 0xCD, 0x05, 0x00, 0xC9
    db 'Hello from the Z80$'
%endif
zprog_len equ $ - zprog


section .rodata
    ; the script pads the image to IMG_SECTORS sectors and this must fit
img_end:
section .text
