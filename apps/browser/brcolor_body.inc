; part 7's colour body (SPEC.md 71.34) - assembled by brext_part.asm
brc_f_match:
    call brc_match
    retf

; --- brc_f_theme - the links' colour, from the SYSTEM's theme (71.34) -------
; A link is in the colour the theme gives a focused title bar - its accent -
; under Color and Windows, read off SYSTEM.CFG's 'TH' (the theme) and 'TP'
; (the preset's colours, byte 2 the active title's ground, SPEC.md
; 76.14.14); blue under Bright and Dark, on kern_mega, and wherever the file
; will not say. UI task: it reads a disk. out AL; all else kept
CFG_BUFKB   equ 2
brc_f_theme:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push ds
    push es
    mov ds, [cs:BRCL_SELF]
    mov byte [brc_acc], 1           ; CBLUE
    call OSAPI_CPU_INFO
    test ah, CPU_F_PM
    jnz .out                        ; a selector's sum is nothing: blue
    call OSAPI_FILE_HERE            ; bank where we stand
    mov [brc_cwd], dx
    mov [brc_drv], bl
    call OSAPI_VOL_SYS              ; the system disk's root
    xor dx, dx
    call OSAPI_FILE_GOTO_QM
    jc .home
    mov ax, ds
    mov dx, brc_buf + 15
    mov cl, 4
    shr dx, cl
    add ax, dx
    add ax, 31
    and ax, 0FFE0h                  ; 512-byte aligned (SPEC.md 22.5)
    mov es, ax
    xor bx, bx
    mov cx, CFG_BUFKB * 1024
    xor dx, dx
    mov si, brc_cfgname
    call OSAPI_FILE_READ            ; DX:AX = its bytes
    jc .home
    or dx, dx
    jnz .home
    call brc_cfg                    ; ES:0..AX the file
.home:
    mov dx, [brc_cwd]
    mov bl, [brc_drv]
    call OSAPI_FILE_GOTO_QM
    jnc .out
    call OSAPI_FILE_GOTO
.out:
    mov al, [brc_acc]
    pop es
    pop ds
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    retf

; brc_cfg - ES:0, AX bytes = SYSTEM.CFG: its 'TH' and 'TP' records
brc_cfg:
    cmp ax, 10
    jb .ret
    cmp word [es:0], 'O8'
    jne .ret
    cmp word [es:2], '8C'
    jne .ret
    mov cx, ax                      ; CX = the end
    mov si, 10
    xor dl, dl                      ; DL = the theme, Bright until 'TH'
    mov dh, 0FFh                    ; DH = the accent, none until 'TP'
.rec:
    lea bx, [si + 4]
    cmp bx, cx
    ja .done
    mov ax, [es:si]
    or ax, ax
    jz .done
    mov bl, [es:si + 3]             ; its length
    xor bh, bh
    cmp ax, 'TH'
    jne .tp
    cmp byte [es:si + 2], 2
    jne .next
    mov dl, [es:si + 4]
    jmp short .next
.tp:
    cmp ax, 'TP'
    jne .next
    cmp byte [es:si + 2], 1
    jne .next
    cmp bl, 3
    jb .next
    mov dh, [es:si + 6]
.next:
    add si, 4
    add si, bx
    jmp .rec
.done:
    cmp dl, 2                       ; Color or Windows wear the preset's row
    jb .ret
    cmp dh, 16
    jae .ret
    or dh, dh
    jz .ret                         ; never black: a link must show
    mov [brc_acc], dh
.ret:
    ret

; brc_match - ES:SI..CX = an attribute's value -> CF=0, AL = 0..15
; A name, or six hex digits with or without '#'. AX clobbered
brc_match:
    push bx
    push cx
    push dx
    push si
    push di
    mov di, brc_names
.nent:
    cmp byte [cs:di], 0
    je .hex                         ; not a name
    push si
.ncmp:
    mov al, [cs:di]
    or al, al
    jz .nend
    cmp si, cx
    jae .nskip
    mov ah, [es:si]
    or ah, 20h
    cmp ah, al
    jne .nskip
    inc si
    inc di
    jmp .ncmp
.nend:
    cmp si, cx
    jne .nskip
    pop si
    mov ax, [cs:di+1]
    mov cl, [cs:di+3]
    jmp short .near
.nskip:
    pop si
.nsk2:
    cmp byte [cs:di], 0
    je .nsk3
    inc di
    jmp .nsk2
.nsk3:
    add di, 4
    jmp .nent
.hex:
    cmp byte [es:si], '#'
    jne .h0
    inc si
.h0:
    mov ax, cx
    sub ax, si
    cmp ax, 6
    jb .no
    call brc_hex2
    jc .no
    mov bl, al
    call brc_hex2
    jc .no
    mov bh, al
    call brc_hex2
    jc .no
    mov cl, al
    mov ax, bx
.near:
    call rgb_near
    clc
    jmp short .out
.no:
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; brc_hex2 - ES:SI two hex digits -> AL, SI += 2; CF=1 not hex
brc_hex2:
    push cx
    mov al, [es:si]
    call brc_nib
    jc .o
    mov cl, 4
    shl al, cl
    mov ch, al
    mov al, [es:si+1]
    call brc_nib
    jc .o
    or al, ch
    add si, 2
    clc
.o:
    pop cx
    ret

brc_nib:
    cmp al, '9'
    jbe .dig
    or al, 20h
    sub al, 'a' - 10
    cmp al, 10
    jb .bad
    cmp al, 16
    jae .bad
    clc
    ret
.dig:
    sub al, '0'
    jb .bad
    clc
    ret
.bad:
    stc
    ret

%include "os88rgb.inc"

brc_cfgname: db 'SYSTEM.CFG', 0
brc_acc:    db 1
brc_drv:    db 0
brc_cwd:    dw 0

; name, NUL, R, G, B: the sixteen HTML 3.2 names and a few the period used
brc_names:
    db 'black', 0, 0, 0, 0
    db 'silver', 0, 0C0h, 0C0h, 0C0h
    db 'gray', 0, 80h, 80h, 80h
    db 'grey', 0, 80h, 80h, 80h
    db 'white', 0, 0FFh, 0FFh, 0FFh
    db 'maroon', 0, 80h, 0, 0
    db 'red', 0, 0FFh, 0, 0
    db 'purple', 0, 80h, 0, 80h
    db 'fuchsia', 0, 0FFh, 0, 0FFh
    db 'magenta', 0, 0FFh, 0, 0FFh
    db 'green', 0, 0, 80h, 0
    db 'lime', 0, 0, 0FFh, 0
    db 'olive', 0, 80h, 80h, 0
    db 'yellow', 0, 0FFh, 0FFh, 0
    db 'navy', 0, 0, 0, 80h
    db 'blue', 0, 0, 0, 0FFh
    db 'teal', 0, 0, 80h, 80h
    db 'aqua', 0, 0, 0FFh, 0FFh
    db 'cyan', 0, 0, 0FFh, 0FFh
    db 'orange', 0, 0FFh, 0A5h, 0
    db 'darkblue', 0, 0, 0, 8Bh
    db 'darkred', 0, 8Bh, 0, 0
    db 0

brc_buf:    times CFG_BUFKB * 1024 + 512 + 16 db 0 ; SYSTEM.CFG, read here
