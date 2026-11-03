; =============================================================================
; os8088 - tests/dostrap/bigtail.asm - BIGTAIL.EXE, an .EXE whose FILE is far
; bigger than its LOAD MODULE (SPEC.md 96.8.2)
;
; DOS loads an MZ file's header + image - e_cp pages, the last one e_cblp
; bytes long - and nothing after it. Everything past that is the program's
; own business: overlays, an appended archive, Works 1.05's 316KB of program
; behind a 2.8KB stub. tests/dosbigtail.py builds this file and then appends
; a PAYLOAD to it, so the file on the disk is the module below followed by
; TAIL_WORDS little-endian words, word j being (j & 0FFFFh) XOR (5A5Ah when
; j >= 65536). The payload is NOT committed: the row writes it.
;
; Run, it:
;   - checks its one relocation was applied (a far pointer that reads back
;     RELOC-OK) - the load-module read has to deliver the relocation table
;     and the image exactly as the whole-file read did;
;   - opens its own file by name, AH=3Dh in the current directory;
;   - seeks to three places in the payload - its first word, one past 64K
;     words, its last eight - and reads 16 bytes at each with AH=3Fh,
;     comparing every word with the formula: `TAIL n OK` or `TAIL n BAD`;
;   - prints `BIGTAIL READY` and exits with 0, or with 1 if anything failed.
;
; The module is 3,000 bytes so its last page is PARTIAL (e_cblp = 440): the
; loader reads whole sectors, so the payload's first 72 bytes land in the
; block's slack, which is the case the rounding has to get right.
; It runs under a real IBM DOS 3.30 unchanged.
; =============================================================================

    cpu 8086
    bits 16

TAIL_WORDS equ 102400               ; 200KB of payload - the row's constant too
MODSZ      equ 3000                 ; header + image, exactly

section .hdr start=0
    dw 0x5A4D                       ; e_magic
    dw MODSZ % 512                  ; e_cblp: a PARTIAL last page
    dw (MODSZ + 511) / 512          ; e_cp
    dw 1                            ; e_crlc
    dw 32                           ; e_cparhdr: 512 bytes of header
    dw 0x0040                       ; e_minalloc: 1KB past the image, the stack
    dw 0xFFFF                       ; e_maxalloc
    dw IMGPARA                      ; e_ss, relative to the load segment
    dw 0x0400                       ; e_sp
    dw 0                            ; e_csum
    dw entry                        ; e_ip
    dw 0                            ; e_cs
    dw 0x001C                       ; e_lfarlc
    dw 0                            ; e_ovno
    dw dseg_ptr, 0                  ; THE relocation
    times 512 - ($ - $$) db 0

section .img start=512 vstart=0
entry:
    push cs
    pop ds
    mov dx, s_hi
    call puts

    ; --- 1. the relocation -------------------------------------------------
    mov es, [dseg_ptr]              ; the word the loader fixed up
    mov dx, s_rok
    cmp word [es:0], 'RE'
    jne .relbad
    cmp word [es:2], 'LO'
    je .relput
.relbad:
    mov byte [bad], 1
    mov dx, s_rbad
.relput:
    call puts

    ; --- 2. our own file ---------------------------------------------------
    mov dx, s_name
    mov ax, 0x3D00                  ; open, read-only
    int 0x21
    jnc .opened
    mov dx, s_noopen
    call puts
    mov byte [bad], 1
    jmp .done
.opened:
    mov [hnd], ax

    ; --- 3. three places in the payload -------------------------------------
    xor bp, bp                      ; the probe's index into the table
.probe:
    mov ax, [probes + bp]           ; word index, low...
    mov cx, [probes + bp + 2]       ; ...and high
    mov [jlo], ax
    mov [jhi], cx
    shl ax, 1                       ; -> a byte offset, then past the module
    rcl cx, 1
    add ax, MODSZ
    adc cx, 0
    mov dx, ax                      ; CX:DX = the offset AH=42h takes
    mov bx, [hnd]
    mov ax, 0x4200
    int 0x21
    jc .pbad
    mov bx, [hnd]
    mov cx, 16
    mov dx, buf
    mov ah, 0x3F
    int 0x21
    jc .pbad
    cmp ax, 16
    jne .pbad
    mov si, buf
    mov cx, 8
.word:
    mov ax, [jlo]                   ; the formula, for word j
    cmp word [jhi], 0
    je .low
    xor ax, 0x5A5A
.low:
    cmp [si], ax
    jne .pbad
    add si, 2
    add word [jlo], 1
    adc word [jhi], 0
    loop .word
    mov dx, s_tok
    jmp short .pput
.pbad:
    mov byte [bad], 1
    mov dx, s_tbad
.pput:
    mov ax, bp                      ; `TAIL n`, n = the probe's number
    shr ax, 1
    shr ax, 1
    add al, '0'
    mov [s_tn + 5], al
    push dx
    mov dx, s_tn
    call puts
    pop dx
    call puts
    add bp, 4
    cmp bp, NPROBES * 4
    jb .probe

    mov bx, [hnd]
    mov ah, 0x3E
    int 0x21

.done:
    mov dx, s_ready
    call puts
    mov al, [bad]
    mov ah, 0x4C
    int 0x21

puts:
    mov ah, 0x09
    int 0x21
    ret

probes:
    dd 0                            ; the first word
    dd 65536 + 3                    ; one past 64K words, past the XOR's edge
    dd TAIL_WORDS - 8               ; the last eight
NPROBES equ ($ - probes) / 4

dseg_ptr: dw DATASEG                ; relative to the image: the loader adds
                                    ; the load segment, or this names the IVT
hnd:    dw 0
jlo:    dw 0
jhi:    dw 0
bad:    db 0
buf:    times 16 db 0

s_hi:    db 'BIGTAIL START', 13, 10, '$'
s_rok:   db 'RELOC OK', 13, 10, '$'
s_rbad:  db 'RELOC BAD', 13, 10, '$'
s_name:  db 'BIGTAIL.EXE', 0
s_noopen: db 'OPEN BAD', 13, 10, '$'
s_tn:    db 'TAIL 0 $'
s_tok:   db 'OK', 13, 10, '$'
s_tbad:  db 'BAD', 13, 10, '$'
s_ready: db 'BIGTAIL READY', 13, 10, '$'

    align 16, db 0
datasec:
    db 'RELOC-OK'
DATASEG equ (datasec - $$) >> 4

    times MODSZ - 512 - ($ - $$) db 0

IMGPARA equ (MODSZ - 512 + 15) / 16
