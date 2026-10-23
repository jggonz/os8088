; =============================================================================
; os8088 - tests/dostrap/xunp.asm
;
; XUNP.EXE - SPEC.md 96.40.9.1's SECOND probe: a program the loader has to
; UNPACK (kerndos/kdxpack.inc). This file is the PLAIN program - an MZ .EXE
; with three relocations - and `tools/os88xpack.py` packs it into the EXEPACK
; layout, whose stub is a refusal: it prints "Packed file is corrupt" and
; exits 0FFh. So the only way this program's own code runs at all is through
; a loader that unpacked it and relocated it, and what the code checks is that
; it did both correctly.
;
;   make build/XUNP.EXE      (and XUNPL.EXE, the 18-byte header with a skip
;                             of 2; XUNPT.EXE and XUNPS.EXE, the table and the
;                             stream spoiled, which the loader must REFUSE)
;
; THE IMAGE is 128KB and two 64KB groups, so the packed table has entries in
; group 0 and group 1, one of them at offset 0FFFFh - the word that straddles
; a paragraph - and the stream is copies and fills, one of them a run of a
; byte that is not zero and one of them longer than a command can say:
;
;   CSEG = paragraph 0       the code; `mov ax, DSEG` relocated (group 0)
;   0400h  pattern A         64 bytes of i*7+3, a COPY
;   0440h  run R             300 bytes of 5Ah, a FILL
;          ...zeros...       to 1FF00h: two fills, 0FFFFh and the rest
;   DSEG = paragraph 1FF0h   +10h dw CSEG (group 1, offset FF10h); +20h
;                            pattern B, 0CFh bytes of i*13+1; +0FFh dw CSEG,
;                            the straddler (group 1, offset FFFFh); +110h
;                            pattern C, 32 bytes of 0FFh-i
;   SSEG = paragraph 2020h   the stack, past the image (minalloc)
;
; Exit codes: 30h every check held. 31h CS is not the load segment, 32h SS is
; not, 33h the code's fixup, 34h the straddling fixup, 35h the group-1 fixup,
; 36h pattern A, 37h the run, 38h the zeros, 39h pattern B, 3Ah pattern C.
; 0FFh is the STUB: the loader did not unpack it.
;
; OURS, MIT with the rest of the tree, under tests/ because it is not shipped.
; =============================================================================

    cpu 8086
    bits 16

DSEG    equ 0x1FF0
SSEG    equ 0x2020
IMGLEN  equ 0x20200                 ; through pattern C's paragraphs
RUNEND  equ 0x400 + 64 + 300        ; the run's end (asserted at the label)

section .hdr start=0
    dw 0x5A4D
    dw FILESZ % 512
    dw (FILESZ + 511) / 512
    dw 3                            ; three relocations
    dw 4                            ; 64 bytes of header
    dw 0x0020                       ; minalloc: the stack
    dw 0xFFFF
    dw SSEG
    dw 0x0200
    dw 0
    dw entry                        ; IP
    dw 0                            ; CS
    dw 0x001C                       ; the table
    dw 0
    dw fix1, 0                      ; CSEG:fix1
    dw 0x0010, DSEG                 ; DSEG:0010
    dw 0x00FF, DSEG                 ; DSEG:00FF - linear 1FFFFh
    times 64 - ($ - $$) db 0

section .img start=64 vstart=0
entry:
    mov ax, es                      ; the PSP, as DOS hands it over
    add ax, 0x10
    mov bp, ax                      ; BP = L, the load segment
    push cs
    pop ds
    mov [lseg], ax
    mov al, 0x31
    mov bx, cs
    cmp bx, bp
    jne fail
    mov al, 0x32
    mov bx, ss
    sub bx, bp
    cmp bx, SSEG
    jne fail
    mov al, 0x33
    db 0xBB                         ; mov bx, imm16 - THE FIXUP
fix1: dw DSEG
    sub bx, bp
    cmp bx, DSEG
    jne fail
    mov es, [fix1]                  ; ES = DSEG, relocated
    mov al, 0x34
    cmp [es:0x00FF], bp
    jne fail
    mov al, 0x35
    cmp [es:0x0010], bp
    jne fail
    ; --- pattern A, i*7+3 -----------------------------------------------------
    mov al, 0x36
    mov si, pat_a
    mov cx, 64
    mov bl, 3
.a:
    cmp [si], bl
    jne fail
    add bl, 7
    inc si
    loop .a
    ; --- the run of 5Ah -------------------------------------------------------
    mov al, 0x37
    mov cx, 300
.r:
    cmp byte [si], 0x5A
    jne fail
    inc si
    loop .r
    ; --- zeros, sampled every 256 bytes up to DSEG ---------------------------
    mov al, 0x38
    mov dx, ds
    add dx, (RUNEND + 15) / 16
.z:
    mov ds, dx
    cmp byte [0], 0
    jne fail
    cmp byte [15], 0
    jne fail
    add dx, 16
    mov bx, dx
    sub bx, bp
    cmp bx, DSEG
    jb .z
    push cs
    pop ds
    ; --- pattern B, i*13+1 ----------------------------------------------------
    mov al, 0x39
    mov si, 0x20
    mov cx, 0xCF
    mov bl, 1
.b:
    cmp [es:si], bl
    jne fail
    add bl, 13
    inc si
    loop .b
    ; --- pattern C, 0FFh-i ----------------------------------------------------
    mov al, 0x3A
    mov si, 0x110
    mov cx, 32
    mov bl, 0xFF
.c:
    cmp [es:si], bl
    jne fail
    dec bl
    inc si
    loop .c
    mov dx, s_ok
    mov ah, 0x09
    int 0x21
    mov ax, 0x4C30
    int 0x21
fail:
    push cs
    pop ds
    mov [code], al
    mov dx, s_bad
    mov ah, 0x09
    int 0x21
    mov al, [code]
    mov ah, 0x4C
    int 0x21

lseg:   dw 0
code:   db 0
s_ok:   db 'XUNP: unpacked and relocated', 13, 10, '$'
s_bad:  db 'XUNP: a check failed - see the exit code', 13, 10, '$'

    times 0x400 - ($ - $$) db 0
pat_a:
%assign i 0
%rep 64
    db (i * 7 + 3) & 0xFF
%assign i i + 1
%endrep
    times 300 db 0x5A
%if ($ - $$) != RUNEND
 %error "RUNEND is where the run ends"
%endif
    times DSEG * 16 - ($ - $$) db 0

section .dat follows=.img vstart=0
    times 0x10 db 0
    dw 0                            ; +10h CSEG, relocated
    times 0x20 - ($ - $$) db 0
%assign i 0
%rep 0xCF
    db (i * 13 + 1) & 0xFF
%assign i i + 1
%endrep
    times 0xFF - ($ - $$) db 0x77
    dw 0                            ; +0FFh CSEG, relocated: linear 1FFFFh
    times 0x110 - ($ - $$) db 0x66
%assign i 0
%rep 32
    db 0xFF - i
%assign i i + 1
%endrep
    times IMGLEN - DSEG * 16 - ($ - $$) db 0

FILESZ  equ 64 + IMGLEN
