; =============================================================================
; os8088 - tests/dostrap/xpack.asm
;
; XPACK.EXE - SPEC.md 96.40.9.1's probe: a program SHAPED like one EXEPACK
; packed, doing the one thing EXEPACK's unpacker does that a DOS with A20 open
; breaks. It is not packed and carries none of EXEPACK's code - only the shape
; a loader recognises it by, which is the point: the loader has to see it.
;
;   nasm -f bin -w+error -o XPACK.EXE tests/dostrap/xpack.asm
;   make build/XPACK.EXE              (and build/XPACKNS.EXE, the control)
;
; THE SHAPE: an MZ header with no relocation entries, the entry at CS:0010,
; and 'RB' at CS:000E - the end of EXEPACK's 16-byte header, which is what
; MS-DOS 5's loader and kern_dos's `kd_xpack` both key on.
;
; THE HAZARD: EXEPACK normalises its pointers DOWNWARD - a segment `sub` and
; `or si, -16` - so near the bottom of its output it addresses the load
; segment B as (B - 0FFFh):FFF0. Below 64KB that segment is below zero and
; the address is correct ONLY through the 1MB wrap. This probe makes exactly
; that access, to its own first eight bytes, and compares them with the same
; bytes read directly. With A20 shut (any 8088, any DOS with DOS=LOW) the wrap
; makes them one; loaded above 64KB there is no wrap to need; loaded below it
; with A20 OPEN - kern_mega's kern_dos in the HMA, before 96.40.9.1 - the read
; lands in the HMA and they differ, which is where EXEPACK prints "Packed file
; is corrupt" and exits 255.
;
; Exit codes: 0x30 the wrapped read saw the program (pass); 0xFF it did not
; (and the line says at which PSP). `-DNOSIG` builds XPACKNS.EXE, the same
; program with the signature spoiled: a loader keying on the shape loads it
; LOW, so on kern_mega it must exit 0xFF - the control that says the probe
; can see the hazard at all. Under a real DOS both exit 0x30 on an 8088.
;
; OURS, MIT with the rest of the tree, and under tests/ because it is not
; shipped software (CLAUDE.md, Layout).
; =============================================================================

    cpu 8086
    bits 16

ENTSEG  equ 1                       ; the entry block's paragraph in the image

; --- the MZ header, at file offset 0 ----------------------------------------
section .hdr start=0
    dw 0x5A4D                       ; e_magic
    dw FILESZ % 512                 ; e_cblp
    dw (FILESZ + 511) / 512         ; e_cp
    dw 0                            ; e_crlc: NONE, as EXEPACK's always is
    dw 32                           ; e_cparhdr: 512 bytes of header
    dw 0x0040                       ; e_minalloc: 1KB past the image
    dw 0xFFFF                       ; e_maxalloc
    dw STACKSEG                     ; e_ss
    dw 0x0200                       ; e_sp
    dw 0                            ; e_csum
    dw 0x0010                       ; e_ip: EXEPACK's
    dw ENTSEG                       ; e_cs
    dw 0x001C                       ; e_lfarlc
    dw 0                            ; e_ovno
    times 512 - ($ - $$) db 0

; --- the image: paragraph 0 is the marker the wrapped read must find ---------
section .mark start=512 vstart=0
    db 'XPACKMRK'
    times 16 - ($ - $$) db 0

; --- the entry block, addressed from its own paragraph -----------------------
section .ent follows=.mark vstart=0
    times 14 db 0                   ; the rest of a 16-byte EXEPACK header
%ifdef NOSIG
    db 'rb'                         ; the control: not the signature
%else
    db 'RB'                         ; the signature, at CS:IP-2
%endif
entry:                              ; CS:0010
    mov ax, es                      ; ES = the PSP, as EXEPACK reads it
    mov [cs:pspseg], ax
    add ax, 0x10                    ; B, the load segment
    mov bx, ax
    sub bx, 0x0FFF                  ; THE NORMALISATION: below 0 when B is
    mov ds, bx                      ; under 0FFFh, so (B-0FFFh):FFF0 is B:0
    mov si, 0xFFF0                  ; only through the 1MB wrap
    mov es, ax
    xor di, di
    mov cx, 4
    cld
    repe cmpsw
    push cs
    pop ds
    jne .bad
    mov dx, s_ok
    mov ah, 0x09
    int 0x21
    mov ax, 0x4C30
    int 0x21
.bad:
    mov dx, s_bad
    mov ah, 0x09
    int 0x21
    mov ax, [pspseg]
    mov cx, 4
.hex:
    push cx
    mov cl, 4
    rol ax, cl
    pop cx
    push ax
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .dig
    add al, 7
.dig:
    mov dl, al
    mov ah, 0x02
    int 0x21
    pop ax
    loop .hex
    mov dx, s_crlf
    mov ah, 0x09
    int 0x21
    mov ax, 0x4CFF
    int 0x21

pspseg: dw 0
s_ok:   db 'XPACK: the wrapped read saw the program', 13, 10, '$'
s_bad:  db 'XPACK: the wrapped read missed it - A20 open below 64KB, PSP $'
s_crlf: db 13, 10, '$'

    align 16
ENTEND  equ $ - $$
section .stk follows=.ent vstart=0
    times 0x200 db 0
STKEND  equ $ - $$

STACKSEG equ ENTSEG + ENTEND / 16
FILESZ   equ 512 + 16 + ENTEND + STKEND
