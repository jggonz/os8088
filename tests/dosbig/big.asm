; =============================================================================
; os8088 - tests/dosbig/big.asm
;
; A DOS .EXE too big to load until kern_dos GIVES THE CACHE BACK (SPEC.md
; 96.44.11.1). It exists for one path and there is no other way to reach it:
; `.loadtry` in `kerndos/kdentry.inc` sheds a rung of the read-ahead and reads
; again, and the only thing that can make `dos_load` refuse in the first place
; is a file between the arena's capacity WITH the cache and its capacity
; without.
;
; THE SIZE IS THE WHOLE FIXTURE. Measured on the machine (a 640KB 5150, both
; the 360KB and the mixed-geometry profiles, which agree to the byte),
; `dos_load`'s capacity is `([dos_ldpara] - 16) * 16` and the ladder moves it:
;
;     cache held   capacity
;      32 KB        569,952      <- the width the mount arms
;      18 KB        584,288      <- KD_RAH_L1
;       9 KB        593,504      <- KD_RAH_L2
;       none        602,720
;
; BIGSZ sits at the middle of 569,952 .. 602,720, so this file is refused
; twice and loads on the third try - and there is ~16KB of slack on each side,
; which is sixteen rungs of KD_IMG_KB in either direction. `tests/kdbigexe.py`
; re-derives all four numbers off the running guest and says which way the
; fixture has drifted if it ever stops landing between them, so this never
; fails as a mystery.
;
; What it asserts about the load itself, because a row that only proves the
; program STARTED would pass on a loader that read half the file:
;
;   - the first eight bytes of the IMAGE, 585KB below the code that reads
;     them, so a short read or a `dos_movedown` that gave up at 64KB is
;     caught rather than assumed;
;   - a relocation applied at the far end of a 586KB image;
;   - a marker at image offset 0FEFCh, which is PSP:FFFC - the one place a
;     loader is tempted to write INTO an .EXE, because it is where a .COM's
;     stack word goes (SPEC.md 96.3.1). It cost Test Drive III two bytes of a
;     routine and read as a freeze at the menu two minutes later;
;   - and PSP:0002, which is the arena the retries left behind.
;
; OURS, MIT with the rest of the tree, and under tests/ because it is not
; shipped software (CLAUDE.md, Layout).
; =============================================================================

    cpu 8086
    bits 16

%ifndef BIGSZ
%define BIGSZ 586752                ; the Makefile passes it; this is the same
%endif                              ; number, so the file assembles by hand

HDRSZ   equ 512                     ; 32 paragraphs of header
TAILSZ  equ 1024                    ; the last KB: code, strings and the stack
IMGSZ   equ BIGSZ - HDRSZ

; --- WHERE A .COM's STACK WORD WOULD LAND IN AN .EXE (SPEC.md 96.3.1) -------
; DOS gives a .COM `SP = FFFEh` and pushes a zero word under it, at PSP:FFFC.
; An .EXE has its own SS:SP out of its header and DOS writes nothing - and an
; .EXE image begins at PSP + 10h paragraphs, so PSP:FFFC is image offset
; 0FFFCh - 100h = 0FEFCh, which for anything bigger than 64KB is code or data
; the program is going to use.
;
; **A PAD OF ZEROS CANNOT SEE THAT WRITE**, which is why this fixture missed
; it for a whole cycle: the word went into the middle of 585KB of zeros and
; changed nothing observable. A marker is the whole fix.
COMSTK  equ 0xFEFC
COMSTKV equ 0xC0DE
PADSZ   equ IMGSZ - TAILSZ
CODESEG equ PADSZ / 16              ; where the tail is, relative to the load
                                    ; segment - ~36,600, which is why this has
%if BIGSZ % 512                     ; to be a word and not a byte count
%error "BIGSZ must be a whole number of 512-byte pages: e_cblp is 0 below, \
which is the encoding that means a FULL last page (SPEC.md 96.8)"
%endif
%if PADSZ % 16
%error "the pad has to be a whole number of PARAGRAPHS or CODESEG is not the \
tail's own segment"
%endif
%if COMSTK + 2 > PADSZ
%error "COMSTK must fall inside the pad - the marker is the whole point"
%endif

; --- the MZ header ----------------------------------------------------------
section .hdr start=0
    dw 0x5A4D                       ; e_magic
    dw 0                            ; e_cblp: the last page is FULL
    dw BIGSZ / 512                  ; e_cp: pages, header included
    dw 1                            ; e_crlc
    dw HDRSZ / 16                   ; e_cparhdr
    dw 0                            ; e_minalloc: NOTHING past the image. The
                                    ; stack is inside it, so a machine that can
                                    ; hold the file can run the program - which
                                    ; is the question this fixture is asking
    dw 0xFFFF                       ; e_maxalloc
    dw CODESEG                      ; e_ss: the tail
    dw TAILSZ                       ; e_sp: its top
    dw 0                            ; e_csum
    dw entry                        ; e_ip
    dw CODESEG                      ; e_cs
    dw 0x001C                       ; e_lfarlc
    dw 0                            ; e_ovno
    dw dseg_ptr, CODESEG            ; THE relocation, at the far end of a 586KB
                                    ; image: the word at CODESEG:dseg_ptr holds
                                    ; a segment and wants the load segment
                                    ; added to it
    times HDRSZ - ($ - $$) db 0

; --- the image: a marker, then the pad --------------------------------------
section .pad start=HDRSZ
head:
    db 'BIGHEAD!'                   ; image offset 0, and the code at the far
    times COMSTK - ($ - $$) db 0    ; end reads it back
comstk:
    dw COMSTKV                      ; image offset 0FEFCh: PSP:FFFC
    times PADSZ - ($ - $$) db 0

; --- ...and the tail, addressed from its own paragraph ----------------------
section .img start=(HDRSZ + PADSZ) vstart=0
entry:
    mov [cs:psp], ds                ; DS = ES = the PSP on an .EXE entry
    push cs
    pop ds

    mov dx, msg_hi
    mov ah, 0x09
    int 0x21

    ; --- 1. the first eight bytes of the image, 585KB below this -----------
    mov dx, msg_head
    mov ah, 0x09
    int 0x21
    mov ax, cs
    sub ax, CODESEG                 ; ...which is where the image begins
    mov es, ax
    xor si, si
    mov di, head_want
    mov cx, 8
.hcmp:
    mov al, [es:si]
    cmp al, [di]
    jne .hbad
    inc si
    inc di
    loop .hcmp
    mov dx, msg_ok
    jmp short .hsay
.hbad:
    mov dx, msg_bad
.hsay:
    mov ah, 0x09
    int 0x21

    ; --- 2. the relocation, at the far end ---------------------------------
    mov dx, msg_rel
    mov ah, 0x09
    int 0x21
    mov ax, [dseg_ptr]              ; the loader added the load segment to a
    mov bx, cs                      ; word that held CODESEG, so it is CS now
    cmp ax, bx
    mov dx, msg_ok
    je .rsay
    mov dx, msg_bad
.rsay:
    mov ah, 0x09
    int 0x21

    ; --- 3. the .COM stack word, which an .EXE does not have ---------------
    ; The one place a loader is tempted to write into an .EXE's own image.
    mov dx, msg_stk
    mov ah, 0x09
    int 0x21
    mov ax, cs
    sub ax, CODESEG                 ; ...the image's own segment again
    mov es, ax
    mov ax, [es:COMSTK]
    cmp ax, COMSTKV
    mov dx, msg_ok
    je .ssay
    mov dx, msg_bad
.ssay:
    mov ah, 0x09
    int 0x21

    ; --- 4. ...and what the retries left in the block -----------------------
    mov dx, msg_mem
    mov ah, 0x09
    int 0x21
    mov es, [psp]
    mov ax, [es:2]                  ; the paragraph past the block (SPEC.md
    sub ax, [psp]                   ; 96.3)
    mov cl, 6
    shr ax, cl                      ; paragraphs -> KB
    call put_dec
    mov dx, msg_kb
    mov ah, 0x09
    int 0x21

    mov dx, msg_rdy
    mov ah, 0x09
    int 0x21
    mov ah, 0x08                    ; park, so the row can read the machine
    int 0x21
    mov ax, 0x4C2A                  ; ...and 42, as every other fixture here
    int 0x21

; -----------------------------------------------------------------------------
; put_dec - AX (< 65536) in decimal, through AH=02h
put_dec:
    push ax
    push bx
    push cx
    push dx
    mov bx, 10
    xor cx, cx
.div:
    xor dx, dx
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .div
.out:
    pop dx
    add dl, '0'
    mov ah, 0x02
    int 0x21
    loop .out
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
psp:        dw 0
dseg_ptr:   dw CODESEG              ; ...and the loader relocates this
head_want:  db 'BIGHEAD!'
msg_hi:     db 13,10,'os8088 DOS gate - BIG.EXE',13,10,13,10,'$'
msg_head:   db 'image head 585KB down: ','$'
msg_rel:    db 'relocation at the far end: ','$'
msg_stk:    db 'the .COM stack word: ','$'
msg_ok:     db 'OK',13,10,'$'
msg_bad:    db 'FAILED',13,10,'$'
msg_mem:    db 'Memory to top of block: ','$'
msg_kb:     db ' KB',13,10,'$'
msg_rdy:    db 13,10,'READY - press a key to exit with code 42',13,10,'$'

    times TAILSZ - ($ - $$) db 0
