; =============================================================================
; os8088 - tests/dosexe/hello.asm
;
; The wave-2 gate's DOS program (SPEC.md 96.8): a real MZ .EXE, hand-built,
; header and all. It is the smallest thing that can tell the difference
; between a loader that works and one that merely starts the program:
;
;   - it reads a far pointer the LOADER had to relocate, and prints the
;     signature behind it. Without the fixup that word is a raw paragraph
;     offset, so DS lands in the interrupt vector table and the signature is
;     whatever happens to be at 0000:0000 - loud, not subtle;
;   - it checks SS:SP is the pair the HEADER asked for and not the PSP's,
;     which is the whole difference between an .EXE and a .COM entry;
;   - its last page is EXACTLY full, so e_cblp is 0 - the encoding that means
;     "512", which a loader that treats 0 as 0 gets wrong by a whole page;
;   - it asks AH=48h for memory and prints what it got, so an allocator that
;     always refuses cannot pass;
;   - and it exits through AH=4Ch with 42.
;
; OURS, MIT with the rest of the tree, and under tests/ because it is not
; shipped software (CLAUDE.md, Layout).
; =============================================================================

    cpu 8086
    bits 16

; --- the MZ header, at file offset 0 ----------------------------------------
section .hdr start=0
    dw 0x5A4D                   ; e_magic
    dw 0                        ; e_cblp: 0 = the last page is FULL (the file
                                ; is padded to a multiple of 512 below, which
                                ; is what makes this the honest encoding)
    dw (FILESZ + 511) / 512     ; e_cp: pages, header included
    dw 1                        ; e_crlc: one relocation
    dw 32                       ; e_cparhdr: 512 bytes of header
    dw 0x0100                   ; e_minalloc: 4KB past the image, which is
                                ; where the stack below lives
    dw 0xFFFF                   ; e_maxalloc: everything
    dw STACKSEG                 ; e_ss, relative to the load segment
    dw 0x0400                   ; e_sp
    dw 0                        ; e_csum
    dw entry                    ; e_ip
    dw 0                        ; e_cs, relative likewise
    dw 0x001C                   ; e_lfarlc
    dw 0                        ; e_ovno
    dw dseg_ptr, 0              ; THE relocation: the word at image:dseg_ptr
                                ; holds a segment and wants the load segment
                                ; added to it
    times 512 - ($ - $$) db 0

; --- the image, addressed from zero -----------------------------------------
section .img start=512 vstart=0
entry:
    mov [cs:save_ss], ss        ; bank what the header handed us BEFORE
    mov [cs:save_sp], sp        ; anything pushes

    push cs
    pop ds

    mov dx, msg_hi
    mov ah, 0x09
    int 0x21

    ; --- 1. the relocated far pointer ------------------------------------
    mov dx, msg_rel
    mov ah, 0x09
    int 0x21
    mov es, [dseg_ptr]          ; the word the loader fixed up
    mov si, 0
    mov cx, 8
.sig:
    mov al, [es:si]
    call put_chr
    inc si
    loop .sig
    call put_crlf

    ; --- 2. SS:SP out of the header --------------------------------------
    mov dx, msg_ss
    mov ah, 0x09
    int 0x21
    mov ax, [save_ss]
    mov bx, cs                  ; SS - CS is the header's own e_ss, because
    sub ax, bx                  ; both were relocated by the same load segment
    call put_hex
    mov al, ':'
    call put_chr
    mov ax, [save_sp]
    call put_hex
    call put_crlf

    ; --- 3. shrink, then allocate: the C runtime's own sequence -----------
    ; A program given maxalloc=FFFFh owns everything, so AH=48h has nothing
    ; to hand out until AH=4Ah gives some back. Doing it in that order is
    ; what every compiled program does and what a stub allocator fails.
    mov dx, msg_shrink
    mov ah, 0x09
    int 0x21
    push ds
    mov ax, es                  ; ES is still the PSP from entry... except we
    pop ds                      ; have used it, so take the PSP from the
    mov ah, 0x62                ; block we were given instead
    int 0x21
    jnc .havepsp
    mov bx, cs                  ; no AH=62h: the PSP is 16 paragraphs below
    sub bx, 0x10                ; our own load segment, by construction
.havepsp:
    mov es, bx
    mov bx, 0x0200              ; keep 8KB, give the rest back
    mov ah, 0x4A
    int 0x21
    jnc .shrunk
    mov dx, msg_noshrink
    mov ah, 0x09
    int 0x21
    jmp short .probe
.shrunk:
    mov dx, msg_ok
    mov ah, 0x09
    int 0x21

    mov dx, msg_alloc
    mov ah, 0x09
    int 0x21
    mov bx, 0x0040              ; 64 paragraphs = 1KB
    mov ah, 0x48
    int 0x21
    jc .nomem
    mov es, ax                  ; prove it is writable and ours
    mov word [es:0], 0x5A5A
    cmp word [es:0], 0x5A5A
    jne .nomem
    mov dx, msg_got
    mov ah, 0x09
    int 0x21
    jmp short .probe
.nomem:
    mov dx, msg_nomem
    mov ah, 0x09
    int 0x21
.probe:
    ; ...and the probe every program makes: how much is there?
    mov dx, msg_probe
    mov ah, 0x09
    int 0x21
    mov bx, 0xFFFF
    mov ah, 0x48
    int 0x21
    mov ax, bx                  ; the refusal's answer is the truth we want
    mov cl, 6
    shr ax, cl                  ; paragraphs -> KB
    call put_dec16
    mov dx, msg_kb
    mov ah, 0x09
    int 0x21

    mov dx, msg_key
    mov ah, 0x09
    int 0x21
    mov ah, 0x08
    int 0x21

    mov ax, 0x4C2A              ; exit 42
    int 0x21

; -----------------------------------------------------------------------------
put_chr:
    push ax
    push dx
    mov dl, al
    mov ah, 0x02
    int 0x21
    pop dx
    pop ax
    ret

put_crlf:
    mov al, 13
    call put_chr
    mov al, 10
    call put_chr
    ret

put_hex:                        ; AX as four hex digits
    push ax
    push bx
    push cx
    mov bx, ax
    mov cx, 4
.d:
    rol bx, 1
    rol bx, 1
    rol bx, 1
    rol bx, 1
    mov al, bl
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .ok
    add al, 7
.ok:
    call put_chr
    loop .d
    pop cx
    pop bx
    pop ax
    ret

put_dec16:
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
.emit:
    pop ax
    add al, '0'
    call put_chr
    loop .emit
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
msg_hi:    db 13,10,'os8088 DOS gate - HELLO.EXE (MZ)',13,10,13,10,'$'
msg_rel:   db 'Relocated pointer reads: ','$'
msg_ss:    db 'Header SS:SP was ','$'
msg_shrink: db 'AH=4Ah shrink to 8KB: ','$'
msg_ok:    db 'ok',13,10,'$'
msg_noshrink: db 'REFUSED - the gate has FAILED',13,10,'$'
msg_alloc: db 'AH=48h for 1KB: ','$'
msg_got:   db 'granted and writable',13,10,'$'
msg_nomem: db 'REFUSED - the gate has FAILED',13,10,'$'
msg_probe: db 'Largest free block: ','$'
msg_kb:    db ' KB',13,10,'$'
msg_key:   db 13,10,'READY - press a key to exit with code 42',13,10,'$'

save_ss:   dw 0
save_sp:   dw 0

; THE WORD THE LOADER MUST FIX UP. It holds the paragraph of `datasec` as an
; offset from the image's own base; the loader adds the load segment, and
; `mov es, [dseg_ptr]` then reaches the real thing. Left alone it names a
; paragraph down in the interrupt vector table.
dseg_ptr:  dw DATASEG

    align 16, db 0
datasec:
    db 'RELOC-OK'

DATASEG   equ (datasec - $$) >> 4

    align 16, db 0
IMGRAW    equ $ - $$
    times ((IMGRAW + 511) / 512) * 512 - IMGRAW db 0
IMGEND    equ $ - $$            ; ...AFTER the padding, which is the whole
                                ; point: e_cp counts padded PAGES, so the
                                ; image the loader places is the padded one
STACKSEG  equ IMGEND >> 4       ; the stack sits in the minalloc area, one
                                ; paragraph past the image. Taking this before
                                ; the padding puts SS:SP INSIDE the image and
                                ; the first push eats the code - which reads
                                ; exactly like a loader that placed it wrong
FILESZ    equ 512 + IMGEND
