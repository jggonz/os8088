; =============================================================================
; os8088 - tests/megapfl/megapfl.asm
;
; MEGAPFL, a TEST instrument (nothing under tests/ ships): THE POOL FILLED
; AND GIVEN BACK, then file activity (KERN-MEGA-PLAN 14.176). On kern_mega a
; 128KB OSAPI_MEM_HUGE claim is made in the pool (HUGE_NOBRIDGE) and written,
; the pool is then filled by OSAPI_XMEM_ALLOC blocks - MEMH_AVAIL's DX, the
; pool's best run, each time, until under 8KB of it is left - and the claim
; and every block are freed. Then: MEMH_AVAIL again (the pool's best run is
; what it was before any of it), this package's own file read back whole by
; OSAPI_FILE_READ (packed, so an expansion into this segment, above 1MB on
; kern_mega), its name field held to the image's, and the entry returns its
; window, after which MEGARUN finds the folder's next file and starts it.
;
; It was written as the stripped probe of a reported regression - "after the
; pool's fill-and-free, file activity faults" (FAULT 00 at wm_ptr2idx, 0D at
; wm_strseg). The cause was the probe's: its entry returned BX = MEMH_AVAIL's
; total free KB rather than its window (SPEC.md 20.2: the entry RETURNS BX =
; the window), and the loader's step 9 bound that as a window. -DPFL_BADBX
; builds that probe again: the row's --break bx.
;
; Every answer lands in PF_RES ('PFLRES01') and on E9 as one line, 'MEGAPFL: '
; and the words from +8:
;
;   +8  the pool's best run before, KB (MEMH_AVAIL's DX)
;   +10 the claim's CF      +12 1 window 0 above 1MB, 2 not
;   +14 the filler's KB     +16 its blocks
;   +18 MEMH_AVAIL after the frees: CF   +20 the pool's best run, KB
;   +22 the read's CF       +24 its bytes (low word)
;   +26 1 the name field read back is the image's, 2 not
;   +28 1 = done (written last)
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'MEGAPFL', pf_entry, 0, OS88_STACK_DEFAULT | OS88_HIGH | OS88_HIGHMEM

PF_NOBR     equ 2                   ; HUGE_NOBRIDGE
PF_NFILL    equ 6                   ; filler blocks at most
PF_BUFSZ    equ 4096

pf_entry:
    push si
    push di
    push bp
    push es
    mov di, pf_name                 ; the file we came out of, [es:si] (the
    mov cx, 13                      ; kernel's segment: SPEC.md 20.2)
.nm:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    loop .nm
    pop es
    mov si, pf_tpl
    call OSAPI_WM_CREATE
    jnc .w
    jmp .out0
.w:
    mov [pf_win], bx
    OS88_REGION_MOVABLE
    xor ax, ax                      ; the pool's best run, before
    mov bx, 1                       ; (MEMH_AVAIL)
    call OSAPI_MEM_HUGE
    mov [pf_res+8], dx
    mov ax, 128                     ; 128KB in the pool
    mov bx, PF_NOBR
    call OSAPI_MEM_HUGE
    sbb si, si
    mov [pf_res+10], si
    jnc .c
    jmp .say
.c:
    mov [pf_a], dx
    push dx
    mov bl, SEG_LIN
    call OSAPI_SEG                  ; DX:AX = window 0's base
    mov word [pf_res+12], 2
    jc .lo
    cmp dx, 0x10
    jb .lo
    mov word [pf_res+12], 1
.lo:
    pop dx
    push es                         ; window 0 written whole
    mov es, dx
    xor di, di
    xor cx, cx
    mov al, 0x5A
    cld
.wr:
    stosb
    loop .wr
    pop es
    xor bp, bp                      ; the filler: the pool's best run, again
    mov di, pf_tok                  ; and again
.f:
    cmp bp, PF_NFILL
    jae .fd
    xor ax, ax
    mov bx, 1
    call OSAPI_MEM_HUGE
    jc .fd
    cmp dx, 8
    jb .fd
    add [pf_res+14], dx
    mov ax, dx                      ; DX:AX = its bytes
    mov cl, 10
    shl ax, cl
    mov cl, 6
    shr dx, cl
    call OSAPI_XMEM_ALLOC
    jc .fd
    mov [di], ax
    mov [di+2], dx
    add di, 4
    inc bp
    jmp short .f
.fd:
    mov [pf_res+16], bp
    mov dx, [pf_a]                  ; everything back
    call OSAPI_MEM_FREE
    mov si, pf_tok
.xf:
    cmp si, di
    jae .xfd
    mov ax, [si]
    mov dx, [si+2]
    call OSAPI_XMEM_FREE
    add si, 4
    jmp short .xf
.xfd:
    xor ax, ax                      ; the pool's best run, after
    mov bx, 1
    call OSAPI_MEM_HUGE
    sbb si, si
    mov [pf_res+18], si
    mov [pf_res+20], dx
%ifdef PFL_BADBX
    mov [pf_win], bx                ; THE PROBE AS IT WAS: BX = MEMH_AVAIL's
%endif                              ; total free KB at the entry's ret
    push es                         ; this package's file, read back whole
    push ds
    pop es
    mov si, pf_name
    mov bx, pf_buf
    xor dx, dx
    mov cx, PF_BUFSZ
    call OSAPI_FILE_READ
    pop es
    sbb si, si
    mov [pf_res+22], si
    mov [pf_res+24], ax
    jc .say
    mov si, 16                      ; the name field, against the image's own
    mov di, pf_buf + 16
    mov cx, 16
    mov word [pf_res+26], 1
.cmp:
    mov al, [si]
    cmp al, [di]
    jne .bad
    inc si
    inc di
    loop .cmp
    jmp short .say
.bad:
    mov word [pf_res+26], 2
.say:
    mov word [pf_res+28], 1
    mov si, pf_s_e9
    call pf_dump
    mov bx, [pf_win]                ; SPEC.md 20.2: the entry RETURNS its window
    clc
.out0:
    pop bp
    pop di
    pop si
    ret

pf_paint:
    ret

; pf_dump - SI = a tag: it and PF_RES's words from +8 onto E9, one line
pf_dump:
    lodsb
    or al, al
    jz .w
    out 0xE9, al
    jmp short pf_dump
.w:
    mov si, pf_res+8
    mov cx, 11
.l:
    lodsw
    call pf_hex4
    mov al, ' '
    out 0xE9, al
    loop .l
    mov al, 10
    out 0xE9, al
    ret
pf_hex4:
    push ax
    mov al, ah
    call pf_hex2
    pop ax
pf_hex2:
    push ax
    push cx
    mov ah, al
    mov cl, 4
    shr al, cl
    call .d
    mov al, ah
    and al, 15
    call .d
    pop cx
    pop ax
    ret
.d:
    add al, '0'
    cmp al, '9'
    jbe .p
    add al, 7
.p:
    out 0xE9, al
    ret

pf_tpl:
    dw 8, 400, 200, 40
    dw pf_ttl, pf_paint, 0, 0
pf_ttl:     db 'MegaPfl', 0
pf_s_e9:    db 'MEGAPFL: ', 0

pf_res:     db 'PFLRES01'
            times 22 db 0
pf_name     times 14 db 0
pf_tok      times PF_NFILL * 4 db 0

    OS88_AWORD pf_a                 ; the claim
    OS88_AWORD pf_win               ; the window, returned (SPEC.md 20.2)
    OS88_BSS_ALIGNED pf_buf, PF_BUFSZ ; (aligned: OSAPI_FILE_READ expands a
                                    ; packed file at offset 0 of a paragraph)
