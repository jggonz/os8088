; =============================================================================
; os8088 - tests/dostrap/dwbig.asm - DWBIG.EXE, an .EXE too big for a DOS
; window's default 128K (SPEC.md 96.54.2.1)
;
; Its load module is 136KB of image behind a 512-byte header, and its
; MINALLOC asks 16KB more - so it fails BOTH of the loader's fit tests at
; 128K: dos_load's (the file, header and all, must fit the block) and
; dos_exe_setup's (PSP + image + MINALLOC). That is WordPerfect 4.1's shape -
; a 134,512-byte load module - grown past it, so one fixture says the window
; grows for either test.
;
; Run, it prints `DWBIG READY` and the paragraphs its block holds (AH=4Ah
; with BX = FFFFh answers the largest it could be, which IS its block, it
; being the top one), then waits for a key and exits with it. The stack is
; the MINALLOC region past the image, which is the field's whole point: a
; loader that ignored MINALLOC would hand it a stack in somebody else's RAM.
; It runs under a real IBM DOS 3.30 unchanged.
; =============================================================================

    cpu 8086
    bits 16

IMGSZ      equ 136 * 1024           ; the image: the WordPerfect shape, grown
IMGPARA    equ IMGSZ / 16
MINAL      equ 0x0400               ; 16KB past it, for the stack
MODSZ      equ 512 + IMGSZ          ; header + image, exactly

section .hdr start=0
    dw 0x5A4D                       ; e_magic
    dw MODSZ % 512                  ; e_cblp
    dw (MODSZ + 511) / 512          ; e_cp
    dw 0                            ; e_crlc
    dw 32                           ; e_cparhdr: 512 bytes of header
    dw MINAL                        ; e_minalloc
    dw 0xFFFF                       ; e_maxalloc
    dw IMGPARA                      ; e_ss, relative to the load segment
    dw MINAL * 16 - 2               ; e_sp: the top of MINALLOC
    dw 0                            ; e_csum
    dw entry                        ; e_ip
    dw 0                            ; e_cs
    dw 0x001C                       ; e_lfarlc
    dw 0                            ; e_ovno
    times 512 - ($ - $$) db 0

section .img start=512 vstart=0
entry:
    push cs
    pop ds
    mov bx, 0xFFFF                  ; ES = the PSP on entry: what CAN it be?
    mov ah, 0x4A
    int 0x21                        ; CF = 1, BX = the paragraphs it has
    mov ax, bx
    mov di, s_para + 4
    mov cx, 5
    mov bx, 10
.d:
    xor dx, dx
    div bx
    add dl, '0'
    mov [di], dl
    dec di
    loop .d
    mov dx, s_hi
    mov ah, 0x09
    int 0x21
    xor ah, ah                      ; a key...
    int 0x16
    mov ah, 0x4C                    ; ...is the exit code
    int 0x21

s_hi:   db 'DWBIG READY '
s_para: db '00000 paragraphs', 13, 10, '$'

    times IMGSZ - ($ - $$) db 0
