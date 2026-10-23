; =============================================================================
; os8088 - tests/dostrap/xmsprobe.asm
;
; XMSPROBE.COM - kern_mega's XMS door (SPEC.md 96.15.4,
; docs/plans/KERN-MEGA-DOS-PLAN.md 6 and its wave W3), end to end, and the
; filter in front of the raw BIOS move.
;
;   nasm -f bin -w+error -o XMSPROBE.COM tests/dostrap/xmsprobe.asm
;   make build/XMSPROBE.COM
;
; Every line is printed through INT 21h AH=09h/02h and nothing through the
; BIOS, kdhello.asm's rule: a line on the screen is a DOS call serviced.
;
;   1  int 2Fh AX=4300h says a driver is there, AX=4310h names its entry;
;   2  AH=08h's free KB, and int 15h AH=88h's raw figure (printed, not judged:
;      0 is right on kern_mega and under HIMEM.SYS alike);
;   3  AH=09h takes 4KB; a 4KB pattern goes OUT with AH=0Bh; conventional
;      memory is then WIPED WITH A THIRD VALUE, so a move that did nothing
;      cannot pass; AH=0Bh brings it back; every byte is compared;
;   4  a second 4KB handle, and the pattern moved EXTENDED TO EXTENDED, then
;      back to conventional (wiped again) and compared - the move SPEC.md 96.15
;      refuses in the window and 96.15.4 lifts on kern_mega. A refusal here is
;      reported as `refused` with the XMS error, and fails the probe;
;   4b the first 2KB of handle 2 moved two bytes UP inside it - an overlap
;      with the source first, which XMS guarantees - and compared;
;   9-13 kern_mega's door (KERN-MEGA-DOS-PLAN 10.4): 0Eh's size and locks,
;      0Ch/0Dh and a second 0Dh refused AAh, a move past the HANDLE refused
;      A4h, 0Fh shrinking it, and 08h's largest no more than its total;
;   5  both handles freed with AH=0Ah;
;   6  THE NEGATIVE CONTROL: a raw int 15h AH=87h reading 16 bytes at linear
;      130000h - the bottom of kern_mega's pool, where the park and the
;      kernel's own claims are (KERN-MEGA-DOS-PLAN 5.1) and never the arena.
;      kern_mega's kern_dos must answer CF=1 (AH=03h).
;
; **UNDER A REAL DOS WITH HIMEM.SYS IT RUNS UNCHANGED** (docs/DOS-DEBUGGING.md)
; and steps 1-5 pass there too - but the control is NOT refused, because
; nothing filters the BIOS move under DOS: it reads 16 bytes of whatever is at
; 1.2MB and changes nothing. That is the expected answer there, printed as
; `not refused`, and the exit code says which of the two it saw:
;
;   0x30  steps 1-5 passed and the control was refused   (kern_mega)
;   0x32  ...but int 15h AX=E801h still answered the size the filter hides
;   0x31  steps 1-5 passed and the control was NOT refused (a real DOS)
;   0x41..0x48  the step that failed: 1 no driver, 2 alloc, 3 move out,
;         4 move back, 5 compare, 6 extended to extended, 7 free,
;         8 the overlapping move, 9-13 the four calls above (0x49-0x4D),
;         14 an overlap one byte apart answered other than A8h (0x4E)
;
; The control only READS, so a machine that does not refuse it is not hurt.
;
; NOTHING HERE IS THIRD-PARTY: it is ours, MIT with the rest of the tree.
; =============================================================================
    cpu 8086
    bits 16
    org 0x100

PLEN    equ 4096                 ; the pattern, bytes
CTL_LIN equ 0x130000             ; the negative control's linear address

start:
    mov dx, s_banner
    call puts

    ; --- 1: is there a driver at all? ---------------------------------------
    mov ax, 0x4300
    int 0x2F
    cmp al, 0x80
    je .have
    mov dx, s_nodrv
    call puts
    mov al, 1
    jmp fail
.have:
    mov ax, 0x4310
    int 0x2F                     ; ES:BX = the entry point
    mov [xms], bx
    mov [xms+2], es
    push cs
    pop es

    ; --- 2: what it says it has ---------------------------------------------
    mov dx, s_free
    call puts
    mov ah, 0x08
    call far [xms]               ; AX = largest, DX = total, KB
    mov ax, dx
    call dec
    mov dx, s_kb
    call puts
    mov dx, s_r88
    call puts
    mov ah, 0x88
    int 0x15                     ; AX = KB of raw extended memory
    jnc .r88
    xor ax, ax
.r88:
    call dec
    mov dx, s_kb
    call puts

    ; --- 3: the round trip --------------------------------------------------
    mov dx, 4                    ; KB
    mov ah, 0x09
    call far [xms]
    or ax, ax
    jnz .got1
    mov al, 2
    jmp fail
.got1:
    mov [h1], dx
    call fill                    ; buf = the pattern
    mov word [mv_sh], 0          ; conventional -> handle 1
    mov word [mv_so], buf
    mov [mv_so+2], cs
    mov ax, [h1]
    mov [mv_dh], ax
    mov word [mv_do], 0
    mov word [mv_do+2], 0
    call move
    jnc .out1
    mov al, 3
    jmp fail
.out1:
    call wipe                    ; the third value: a no-op move cannot pass
    call back1                   ; handle 1 -> conventional
    jnc .in1
    mov al, 4
    jmp fail
.in1:
    call check
    jnc .cmp1
    mov al, 5
    jmp fail
.cmp1:
    mov dx, s_rt
    call puts

    ; --- 4: extended to extended --------------------------------------------
    mov dx, 4
    mov ah, 0x09
    call far [xms]
    or ax, ax
    jnz .got2
    mov al, 2
    jmp fail
.got2:
    mov [h2], dx
    mov ax, [h1]                 ; handle 1 -> handle 2
    mov [mv_sh], ax
    mov word [mv_so], 0
    mov word [mv_so+2], 0
    mov ax, [h2]
    mov [mv_dh], ax
    mov word [mv_do], 0
    mov word [mv_do+2], 0
    call move
    jnc .x2
    mov dx, s_xxno
    call puts
    mov al, bl
    call hex2
    call crlf
    mov al, 6
    jmp fail
.x2:
    call wipe
    mov ax, [h2]                 ; ...and handle 2 -> conventional
    mov [h1b], ax
    call backh
    jc .x2bad
    call check
    jnc .x2ok
.x2bad:
    mov al, 6
    jmp fail
.x2ok:
    mov dx, s_xx
    call puts

    ; --- 4b: an OVERLAPPING move, the source first ----------------------------
    ; XMS 3.0 function 0Bh guarantees it; a forward copy smears the first
    ; words over the rest (KERN-MEGA-DOS-PLAN 10.4 item 1). Handle 2's first
    ; 2KB of the pattern moves two bytes up, inside the same handle
    mov ax, [h2]
    mov [mv_sh], ax
    mov word [mv_so], 0
    mov word [mv_so+2], 0
    mov [mv_dh], ax
    mov word [mv_do], 2
    mov word [mv_do+2], 0
    mov word [mv_len], PLEN / 2
    mov word [mv_len+2], 0
    call movel
    jc .ovbad
    call wipe
    mov ax, [h2]
    mov [h1b], ax
    call backh
    jc .ovbad
    cmp word [buf], 0            ; word 0 stays, and words 1..1024 are the
    jne .ovbad                   ; pattern's 0..1023
    mov si, buf + 2
    mov cx, PLEN / 4
    xor ax, ax
.ovc:
    cmp [si], ax
    jne .ovbad
    add si, 2
    add ax, 0x0103
    loop .ovc
    mov dx, s_ov
    call puts
    jmp short .x9
.ovbad:
    mov al, 8
    jmp fail

    ; --- 9: 0Eh on handle 2 says 4KB, 0 locks ---------------------------------
.x9:
    mov dx, [h2]
    mov ah, 0x0E
    call far [xms]
    cmp ax, 1
    jne .bad9
    cmp dx, 4
    jne .bad9
    or bh, bh
    jz .x10
.bad9:
    mov al, 9
    jmp fail
    ; --- 10: 0Ch locks it with an address, 0Dh unlocks, a second 0Dh is AAh -
.x10:
    mov dx, [h2]
    mov ah, 0x0C
    call far [xms]
    cmp ax, 1
    jne .bad10
    or dx, dx                    ; above 1MB: the high word is at least 10h
    jz .bad10
    mov dx, [h2]
    mov ah, 0x0D
    call far [xms]
    cmp ax, 1
    jne .bad10
    mov dx, [h2]
    mov ah, 0x0D
    call far [xms]
    or ax, ax
    jnz .bad10
    cmp bl, 0xAA
    je .x11
.bad10:
    mov al, 10
    jmp fail
    ; --- 11: a move that runs past the HANDLE is refused, A4h -------------------
.x11:
    mov ax, [h2]
    mov [mv_sh], ax
    mov word [mv_so], 2          ; 4KB from offset 2 of a 4KB block
    mov word [mv_so+2], 0
    mov word [mv_dh], 0
    mov word [mv_do], buf
    mov [mv_do+2], cs
    mov word [mv_len], PLEN
    mov word [mv_len+2], 0
    call movel
    jnc .bad11
    cmp bl, 0xA4
    je .x11b
.bad11:
    mov al, 11
    jmp fail
    ; --- 14: an overlap ONE BYTE apart cannot be moved in words, A8h ----------
.x11b:
    mov ax, [h2]                 ; 2KB from offset 0 to offset 1 of one handle
    mov [mv_sh], ax
    mov [mv_dh], ax
    xor ax, ax
    mov [mv_so], ax
    mov [mv_so+2], ax
    mov word [mv_do], 1
    mov [mv_do+2], ax
    mov word [mv_len], 2048
    mov [mv_len+2], ax
    call movel
    jnc .bad14
    cmp bl, 0xA8
    je .x12
.bad14:
    mov al, 14
    jmp fail
    ; --- 12: 0Fh shrinks it to 2KB, and 0Eh says so -----------------------------
.x12:
    mov dx, [h2]
    mov bx, 2
    mov ah, 0x0F
    call far [xms]
    cmp ax, 1
    jne .bad12
    mov dx, [h2]
    mov ah, 0x0E
    call far [xms]
    cmp dx, 2
    je .x13
.bad12:
    mov al, 12
    jmp fail
    ; --- 13: 08h - the largest free block is no more than the total ----------
.x13:
    mov ah, 0x08
    call far [xms]
    cmp ax, dx
    jbe .x13ok
    mov al, 13
    jmp fail
.x13ok:
    push ax
    mov dx, s_lg
    call puts
    pop ax
    call dec
    call crlf
    mov dx, s_xok
    call puts

    ; --- 5: free ------------------------------------------------------------
    mov dx, [h1]
    mov ah, 0x0A
    call far [xms]
    mov cx, ax
    mov dx, [h2]
    mov ah, 0x0A
    call far [xms]
    and ax, cx
    jnz .freed
    mov al, 7
    jmp fail
.freed:
    mov dx, s_freed
    call puts

    ; --- 6: the negative control --------------------------------------------
    mov dx, s_ctl
    call puts
    mov ax, cs                   ; the destination: ctlbuf, as a linear base
    mov dx, ax
    mov cl, 4
    shl ax, cl
    mov cl, 12
    shr dx, cl
    add ax, ctlbuf
    adc dx, 0
    mov [gdt+0x1A], ax
    mov [gdt+0x1C], dl
    mov word [gdt+0x12], CTL_LIN & 0xFFFF
    mov byte [gdt+0x14], CTL_LIN >> 16
    mov word [gdt+0x10], 0xFFFF  ; the two limits and access bytes
    mov word [gdt+0x18], 0xFFFF
    mov byte [gdt+0x15], 0x93
    mov byte [gdt+0x1D], 0x93
    mov si, gdt
    mov cx, 8                    ; words
    mov ah, 0x87
    int 0x15
    jc .refused
    mov dx, s_notref
    call puts
    mov al, 0x31
    jmp short exit
.refused:
    push ax
    mov dx, s_ref
    call puts
    pop ax
    mov al, ah
    call hex2
    mov dx, s_close
    call puts
    mov ax, 0xE801               ; ...and E801h, which would tell the size
    int 0x15                     ; the filter hides: refused on kern_mega
    jc .e801no
    mov dx, s_e801
    call puts
    mov al, 0x32
    jmp short exit
.e801no:
    mov al, 0x30
exit:
    push ax
    mov dx, s_done
    call puts
    pop ax
    mov ah, 0x4C
    int 0x21

fail:                            ; AL = the step
    push ax
    mov dx, s_fail
    call puts
    pop ax
    push ax
    add al, '0'
    mov dl, al
    mov ah, 0x02
    int 0x21
    call crlf
    pop ax
    add al, 0x40
    mov ah, 0x4C
    int 0x21

; --- the move block and its helpers ------------------------------------------
; move - AH=0Bh with mv_*; out CF=1 refused (BL = the code)
move:
    mov word [mv_len], PLEN
    mov word [mv_len+2], 0
movel:                           ; ...or with mv_len already set
    mov si, mv_len
    mov ah, 0x0B
    call far [xms]               ; DS:SI = the block
    cmp ax, 1
    je .ok
    stc
    ret
.ok:
    clc
    ret

; back1 / backh - handle [h1] / [h1b] -> buf
back1:
    mov ax, [h1]
    mov [h1b], ax
backh:
    mov ax, [h1b]
    mov [mv_sh], ax
    mov word [mv_so], 0
    mov word [mv_so+2], 0
    mov word [mv_dh], 0
    mov word [mv_do], buf
    mov [mv_do+2], cs
    jmp short move

; fill / wipe / check - the pattern, the third value, the comparison
fill:
    mov di, buf
    mov cx, PLEN / 2
    xor ax, ax
.f:
    stosw
    add ax, 0x0103               ; no byte of it repeats on a short period
    loop .f
    ret
wipe:
    mov di, buf
    mov cx, PLEN / 2
    mov ax, 0x5A5A
    rep stosw
    ret
check:                           ; CF=1 a byte differs
    mov si, buf
    mov cx, PLEN / 2
    xor ax, ax
.c:
    cmp [si], ax
    jne .bad
    add si, 2
    add ax, 0x0103
    loop .c
    clc
    ret
.bad:
    stc
    ret

; --- printing ------------------------------------------------------------------
puts:
    mov ah, 0x09
    int 0x21
    ret

dec:                             ; AX in decimal
    push ax
    push bx
    push cx
    push dx
    xor cx, cx
    mov bx, 10
.d:
    xor dx, dx
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .d
.e:
    pop dx
    add dl, '0'
    mov ah, 0x02
    int 0x21
    loop .e
    pop dx
    pop cx
    pop bx
    pop ax
    ret

hex2:                            ; AL in hex
    push ax
    mov cl, 4
    shr al, cl
    call .n
    pop ax
    and al, 0x0F
.n:
    add al, '0'
    cmp al, '9'
    jbe .p
    add al, 7
.p:
    mov dl, al
    mov ah, 0x02
    int 0x21
    ret

crlf:
    mov dl, 13
    mov ah, 0x02
    int 0x21
    mov dl, 10
    mov ah, 0x02
    int 0x21
    ret

s_banner: db 'XMSPROBE', 13, 10, '$'
s_nodrv:  db 'XMS: no driver (int 2Fh AX=4300h)', 13, 10, '$'
s_free:   db 'XMS free ', '$'
s_r88:    db 'int 15h AH=88h ', '$'
s_kb:     db ' KB', 13, 10, '$'
s_rt:     db 'round trip 4KB: PASS', 13, 10, '$'
s_xx:     db 'extended to extended: PASS', 13, 10, '$'
s_xxno:   db 'extended to extended: refused, error ', '$'
s_freed:  db 'free: PASS', 13, 10, '$'
s_ov:     db 'overlapping, source first: PASS', 13, 10, '$'
s_lg:     db 'largest free KB ', '$'
s_e801:   db 'int 15h E801h answered after 87h was refused', 13, 10, '$'
s_xok:    db 'lock, unlock, info, bounds, resize: PASS', 13, 10, '$'
s_ctl:    db 'raw AH=87h at 130000h: ', '$'
s_ref:    db 'refused (AH=', '$'
s_close:  db ') PASS', 13, 10, '$'
s_notref: db 'not refused (expected under a real DOS)', 13, 10, '$'
s_fail:   db 'FAIL at step ', '$'
s_done:   db 'XMSPROBE done', 13, 10, '$'

xms:      dd 0
h1:       dw 0
h2:       dw 0
h1b:      dw 0
mv_len:   dd 0                   ; THE XMS MOVE BLOCK, sixteen bytes in this
mv_sh:    dw 0                   ; order (XMS 3.0 function 0Bh)
mv_so:    dd 0
mv_dh:    dw 0
mv_do:    dd 0
gdt:      times 0x30 db 0        ; int 15h AH=87h's six descriptors
ctlbuf:   times 16 db 0

section .bss
buf:      resb PLEN
