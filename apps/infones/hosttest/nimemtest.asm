; =============================================================================
; os8088 - apps/infones/hosttest/nimemtest.asm
;
; A BOOT SECTOR THAT RUNS apps/infones/nimem.inc AND apps/infones/niband.inc ON
; A REAL x86 - the two files where this package loads ES and runs a string
; instruction (SPEC.md 91.14.4). The host harness cannot reach them (it
; substitutes C), tools/cc8086.py never sees hand-written assembly, and a
; routine that got a segment wrong would write into the KERNEL and not fault
; (LESSONS.md 4). So this runs the SHIPPING text - %included, never copied -
; under the one condition that makes this OS unlike every other target this
; code could be tested on: **SS != DS**.
;
; Here CS = DS = 0 stand in for the package's segment (nimem.inc reaches its
; statics through DS, exactly as it does in a package where CS = DS), SS =
; 0x1000 is the task stack, ES = 0x3000 stands in for KERNEL_SEG and MUST come
; back intact, 0x4000 is the machine claim, 0x5000 the CHR claim and 0x6000
; the tile cache.
;
; WHAT EACH CASE CHECKS, and none of it is the routine's own logic restated:
;
;   1. ni_move copies exactly n bytes between two claims and touches nothing
;      either side of them - a guard byte before and after, both checked;
;   2. ni_fill fills exactly n bytes with the byte it was given, guards again;
;   3. ni_pokew / ni_peekw round-trip a LITTLE-ENDIAN word in a claim, which
;      is what the core's PRG window table is made of;
;   4. ni_oam_move puts 256 bytes of the CPU RAM claim into OAM at the
;      documented offset, both halves inside the ONE machine claim - the case
;      where a mover's two segments are the same segment;
;   5. ni_chr_decode turns a 1KB CHR bank into 4,096 bytes of one byte a pixel,
;      checked against HAND-COMPUTED tiles at BOTH ENDS of the bank (tile 0
;      row 0 and tile 63 row 7, which is what catches a loop bound), plus a
;      guard at byte 4,096 - the decoder writing one byte past its claim is
;      exactly the defect no screendump shows;
;   6. after every call ES, DS, BP and SP are what they were AND DF IS CLEAR;
;   7. NEGATIVE CONTROLS: two deliberately broken routines - one that returns
;      with ES left on a claim, one that returns with DF set - go through the
;      same discipline check, which must FAIL both. A harness that cannot see
;      a broken routine has proved nothing (LESSONS.md 2).
;
; RUN IT:  apps/infones/hosttest/nimemtest.sh   (or `make nimemtest`)
; =============================================================================

cpu 8086
bits 16
org 0x7C00

SEG_STACK   equ 0x1000
SEG_KERNEL  equ 0x3000              ; "KERNEL_SEG": it must come back
SEG_MACH    equ 0x4000              ; the machine claim
SEG_CHR     equ 0x5000              ; the CHR claim
SEG_CACHE   equ 0x6000              ; the 32KB tile cache
IMG_SECTORS equ 64

%macro PUSHI 1
    mov ax, %1
    push ax
%endmacro

section .text
section .rodata follows=.text
section .data   follows=.rodata
section .bss    follows=.data nobits
section .text

; -----------------------------------------------------------------------------
; STAGE 1 - the boot sector reads the rest of the image in
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
    mov word [lba], 1
    mov bx, 0x7E00
.rd:
    mov ax, [lba]
    cmp ax, IMG_SECTORS
    jae .go
    call rdsec
    add bx, 512
    inc word [lba]
    jmp .rd
.go:
    jmp 0x0000:body
lba: dw 0

rdsec:
    push ax
    push cx
    push dx
    mov ax, [lba]
    xor dx, dx
    mov cx, 18
    div cx
    mov cl, dl
    inc cl
    mov dh, al
    and dh, 1
    shr ax, 1
    mov ch, al
    mov dl, 0
    mov ax, 0x0201
    int 0x13
    jc .err
    pop dx
    pop cx
    pop ax
    ret
.err:
    mov al, 'D'
    call putc
    jmp halt

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

    mov word [_ni_m + NIM_MACHSEG], SEG_MACH

; --- case 1: ni_move ---------------------------------------------------------
    call disc_arm
    ; fill the source with a walking pattern and put guards round the dest
    mov ax, SEG_CHR
    mov es, ax
    mov di, 0x0100
    mov cx, 300
    mov al, 1
.fill1:
    mov [es:di], al
    inc di
    inc al
    loop .fill1
    mov ax, SEG_CACHE
    mov es, ax
    mov byte [es:0x01FF], 0xAA      ; the guard BEFORE
    mov byte [es:0x0200 + 300], 0xBB ; ...and after
    mov ax, SEG_KERNEL
    mov es, ax
    PUSHI 300
    PUSHI 0x0100
    PUSHI SEG_CHR
    PUSHI 0x0200
    PUSHI SEG_CACHE
    call _ni_move
    add sp, 10
    call disc_check
    ; every byte, and both guards
    push ds
    mov ax, SEG_CACHE
    mov ds, ax
    mov si, 0x0200
    mov cx, 300
    mov al, 1
.cmp1:
    cmp [si], al
    jne .bad1
    inc si
    inc al
    loop .cmp1
    cmp byte [0x01FF], 0xAA
    jne .bad1
    cmp byte [0x0200 + 300], 0xBB
    jne .bad1
    pop ds
    call ok
    jmp c2
.bad1:
    pop ds
    mov si, msg_move
    call bad

; --- case 2: ni_fill ---------------------------------------------------------
c2:
    call disc_arm
    mov ax, SEG_CACHE
    mov es, ax
    mov byte [es:0x0FFF], 0xAA
    mov byte [es:0x1000 + 64], 0xBB
    mov ax, SEG_KERNEL
    mov es, ax
    PUSHI 64
    PUSHI 0x5A
    PUSHI 0x1000
    PUSHI SEG_CACHE
    call _ni_fill
    add sp, 8
    call disc_check
    push ds
    mov ax, SEG_CACHE
    mov ds, ax
    mov si, 0x1000
    mov cx, 64
.cmp2:
    cmp byte [si], 0x5A
    jne .bad2
    inc si
    loop .cmp2
    cmp byte [0x0FFF], 0xAA
    jne .bad2
    cmp byte [0x1000 + 64], 0xBB
    jne .bad2
    pop ds
    call ok
    jmp c3
.bad2:
    pop ds
    mov si, msg_fill
    call bad

; --- case 3: ni_pokew / ni_peekw --------------------------------------------
c3:
    call disc_arm
    PUSHI 0x1234
    PUSHI 0x2000
    PUSHI SEG_MACH
    call _ni_pokew
    add sp, 6
    call disc_check
    push ds
    mov ax, SEG_MACH
    mov ds, ax
    cmp byte [0x2000], 0x34         ; LITTLE-ENDIAN, and the byte order is the
    jne .bad3                       ; whole of what this case is about
    cmp byte [0x2001], 0x12
    jne .bad3
    pop ds
    call disc_arm
    PUSHI 0x2000
    PUSHI SEG_MACH
    call _ni_peekw
    add sp, 4
    cmp ax, 0x1234
    jne .bad3b
    call disc_check
    call ok
    jmp c4
.bad3:
    pop ds
.bad3b:
    mov si, msg_word
    call bad

; --- case 4: ni_oam_move, whose two segments are ONE segment -----------------
c4:
    call disc_arm
    push ds
    mov ax, SEG_MACH
    mov ds, ax
    mov si, 0x0300                  ; a source inside the 2KB CPU RAM
    mov cx, 256
    mov al, 0x40
.f4:
    mov [si], al
    inc si
    inc al
    loop .f4
    mov byte [0x30FF], 0            ; wipe OAM's first and last...
    mov byte [0x3000], 0
    mov byte [0x3100], 0xCC         ; ...and guard the byte after it
    pop ds
    PUSHI 0x0300
    call _ni_oam_move
    add sp, 2
    call disc_check
    push ds
    mov ax, SEG_MACH
    mov ds, ax
    cmp byte [0x3000], 0x40
    jne .bad4
    cmp byte [0x30FF], 0x3F         ; 0x40 + 255, wrapped in a byte
    jne .bad4
    cmp byte [0x3100], 0xCC
    jne .bad4
    pop ds
    call ok
    jmp c5
.bad4:
    pop ds
    mov si, msg_oam
    call bad

; --- case 5: ni_chr_decode, against HAND-COMPUTED tiles ----------------------
; tile 0, row 0:  plane0 = 10110001, plane1 = 11000011
;                 -> 3, 2, 1, 1, 0, 0, 2, 3
; tile 63, row 7: plane0 = 11111111, plane1 = 00001111
;                 -> 1, 1, 1, 1, 3, 3, 3, 3
c5:
    call disc_arm
    push ds
    mov ax, SEG_CHR
    mov ds, ax
    xor si, si                      ; the bank starts clean
    mov cx, 1024
    xor al, al
.z5:
    mov [si], al
    inc si
    loop .z5
    mov byte [0x0000], 0xB1         ; tile 0 plane 0 row 0
    mov byte [0x0008], 0xC3         ; tile 0 plane 1 row 0
    mov byte [1008 + 7], 0xFF       ; tile 63 plane 0 row 7
    mov byte [1008 + 15], 0x0F      ; tile 63 plane 1 row 7
    mov ax, SEG_CACHE
    mov ds, ax
    mov byte [4096], 0xE5           ; THE GUARD one byte past the output
    pop ds
    PUSHI 0
    PUSHI SEG_CHR
    PUSHI 0
    PUSHI SEG_CACHE
    call _ni_chr_decode
    add sp, 8
    call disc_check
    push ds
    mov ax, SEG_CACHE
    mov ds, ax
    mov si, 0
    mov di, t0row0
    mov cx, 8
.k5a:
    mov al, [si]
    cmp al, [cs:di]
    jne .bad5
    inc si
    inc di
    loop .k5a
    mov si, 63 * 64 + 7 * 8
    mov di, t63row7
    mov cx, 8
.k5b:
    mov al, [si]
    cmp al, [cs:di]
    jne .bad5
    inc si
    inc di
    loop .k5b
    cmp byte [4096], 0xE5           ; ...and it wrote nothing past the end
    jne .bad5
    pop ds
    call ok
    jmp c7
.bad5:
    pop ds
    mov si, msg_chr
    call bad

; --- case 7: THE NEGATIVE CONTROLS ------------------------------------------
; A harness that cannot see a broken routine has proved nothing, so two are
; written here on purpose and the discipline check must catch BOTH.
c7:
    call disc_arm
    call neg_es
    call disc_check_expect_fail
    call disc_arm
    call neg_df
    call disc_check_expect_fail
    cld                             ; ...neg_df left it set

; --- the summary -------------------------------------------------------------
    mov si, msg_sum
    call puts
    mov ax, [nfail]
    call putnum
    mov si, msg_sum2
    call puts
    cmp word [nfail], 0
    jne .f
    mov si, msg_ok
    call puts
    jmp halt
.f:
    mov si, msg_bad
    call puts
halt:
    mov si, msg_nl
    call puts
    cli
.h: hlt
    jmp .h

; -----------------------------------------------------------------------------
; THE DISCIPLINE CHECK (case 6, applied to every case)
; -----------------------------------------------------------------------------
disc_arm:
    mov [d_es], es
    mov [d_ds], ds
    mov [d_bp], bp
    mov [d_sp], sp
    ret

disc_check:
    pushf
    push ax
    mov ax, es
    cmp ax, [d_es]
    jne .bad
    mov ax, ds
    cmp ax, [d_ds]
    jne .bad
    cmp bp, [d_bp]
    jne .bad
    mov ax, sp
    add ax, 4                       ; our own pushf and push ax
    cmp ax, [d_sp]
    jne .bad
    pushf
    pop ax
    test ah, 0x04                   ; DF is bit 10, which is bit 2 of AH
    jnz .bad
    pop ax
    popf
    ret
.bad:
    pop ax
    popf
    mov si, msg_disc
    call bad
    ret

; ...and the same check INVERTED, for the two deliberately broken routines
disc_check_expect_fail:
    push ax
    mov ax, es
    cmp ax, [d_es]
    jne .caught
    pushf
    pop ax
    test ah, 0x04
    jnz .caught
    pop ax
    mov si, msg_neg
    call bad
    ret
.caught:
    pop ax
    mov al, 'N'
    call putc
    ret

; the two broken routines. They are HERE and not in the shipping text.
neg_es:
    mov ax, SEG_CACHE
    mov es, ax                      ; ...and never puts it back
    ret
neg_df:
    std                             ; ...and never clears it
    ret

; -----------------------------------------------------------------------------
; REPORTING
; -----------------------------------------------------------------------------
ok:
    mov al, '.'
    call putc
    ret

bad:
    inc word [nfail]
    push ax
    mov al, '!'
    call putc
    call puts
    mov si, msg_nl
    call puts
    pop ax
    ret

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
    push si
.l:
    mov al, [cs:si]
    inc si
    or al, al
    jz .e
    call putc
    jmp .l
.e:
    pop si
    pop ax
    ret

putnum:
    push ax
    push bx
    push cx
    push dx
    mov cx, 0
    mov bx, 10
.dv:
    xor dx, dx
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .dv
.pr:
    pop ax
    add al, '0'
    call putc
    loop .pr
    pop dx
    pop cx
    pop bx
    pop ax
    ret

section .rodata
msg_hello: db 13, 10, "nimemtest: nimem.inc and niband.inc, SS != DS", 13, 10, 0
msg_move:  db "ni_move", 0
msg_fill:  db "ni_fill", 0
msg_word:  db "ni_pokew/ni_peekw", 0
msg_oam:   db "ni_oam_move", 0
msg_chr:   db "ni_chr_decode", 0
msg_disc:  db "ES/DS/BP/SP/DF discipline", 0
msg_neg:   db "a NEGATIVE CONTROL passed the discipline check", 0
msg_sum:   db 13, 10, "nimemtest: ", 0
msg_sum2:  db " failures - ", 0
msg_ok:    db "nimem OK", 0
msg_bad:   db "FAILURES in nimem", 0
msg_nl:    db 13, 10, 0

t0row0:    db 3, 2, 1, 1, 0, 0, 2, 3
t63row7:   db 1, 1, 1, 1, 3, 3, 3, 3

section .bss
nfail: resw 1
d_es:  resw 1
d_ds:  resw 1
d_bp:  resw 1
d_sp:  resw 1

section .text

; =============================================================================
; THE SHIPPING TEXT, %included and never copied
;
; nicpu.inc is here for its .bss register file alone: nimem.inc reads the
; machine claim's segment out of _ni_m, exactly as it does in the package.
; Its two calls out to the C are stubbed below, because this harness has no C
; in it - and stubbing them is honest here, since no case below reaches one.
; =============================================================================
_ni_io_rd:
    xor ax, ax
    ret
_ni_io_wr:
    xor ax, ax
    ret

%include "nicpu.inc"
%include "nimem.inc"
%include "niband.inc"
