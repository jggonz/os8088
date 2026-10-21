; =============================================================================
; os8088 - apps/syscheck/choose.inc - the drive chooser (SPEC.md 118.13)
;
; System Check asks which drive to check before it reads anything. The
; chooser takes the room of the Checks and Findings cards: one row per volume
; this machine has, the selected one in inverse video, and the HARD DISK
; chosen for you - the boot volume when that is a hard disk, else the first
; hard disk, else the boot volume. Scan, Enter or a double-click checks it;
; Scan pressed after a scan opens the chooser again.
; =============================================================================

SC_MAXVOL   equ 26
SC_DBLT     equ 9                   ; a double-click: the same row in 9 ticks

; -----------------------------------------------------------------------------
; sc_volscan - the volumes this machine has, and which one to offer first
; out: sc_vols/sc_vkind/sc_vtran/sc_nvol, sc_vsel = the default, sc_vboot =
;      the boot volume's row (FF none); every register kept
; -----------------------------------------------------------------------------
sc_volscan:
    push ax
    push bx
    push cx
    push dx
    push si
    int 0x11                        ; the BIOS's equipment word: how many
    mov ah, 0                       ; floppy drives, so a one-drive machine's
    test al, 1                      ; B: - an alias of A: (SPEC.md 52.10.5) -
    jz .nf                          ; is not offered
    mov cl, 6
    shr al, cl
    inc ax
    mov ah, al
.nf:
    mov [sc_nflop], ah
    call OSAPI_VOL_SYS              ; BL = the boot volume
    mov byte [sc_vboot], 0FFh
    xor si, si                      ; SI = rows so far
    xor dl, dl                      ; DL = the volume
.vol:
    mov al, dl
    call OSAPI_VOL_KIND             ; AL = medium, AH = transport
    jc .next
    cmp dl, 1
    jne .keep
    cmp byte [sc_nflop], 2
    jb .next
.keep:
    mov [sc_vols+si], dl
    mov [sc_vkind+si], al
    mov [sc_vtran+si], ah
    cmp dl, bl
    jne .hd
    mov cx, si
    mov [sc_vboot], cl
.hd:
    inc si
.next:
    inc dl
    cmp dl, SC_MAXVOL
    jb .vol
    mov cx, si
    mov [sc_nvol], cl
    ; --- the default: the boot volume if it is a hard disk, else the first
    ; hard disk, else the boot volume, else the first row
    mov al, [sc_vboot]
    cmp al, 0FFh
    je .first
    mov bl, al
    xor bh, bh
    cmp byte [sc_vkind+bx], VK_FIXED ; a hard disk is fixed and not a cable
    jne .first                      ; (VT_FILE answers fixed and has no
    cmp byte [sc_vtran+bx], VT_FILE ; sectors of its own)
    jne .set
.first:
    call sc_firsthd                 ; AL = the first hard disk's row, or FF
    cmp al, 0FFh
    jne .set
    mov al, [sc_vboot]
    cmp al, 0FFh
    jne .set
    xor al, al
.set:
    mov [sc_vsel], al
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sc_firsthd - AL = the first row that is a hard disk, FF none (keeps the rest)
sc_firsthd:
    push bx
    push cx
    xor bx, bx
    mov cl, [sc_nvol]
.f:
    cmp bl, cl
    jae .none
    cmp byte [sc_vkind+bx], VK_FIXED
    jne .n
    cmp byte [sc_vtran+bx], VT_FILE
    jne .hit
.n:
    inc bx
    jmp short .f
.none:
    mov bl, 0FFh
.hit:
    mov al, bl
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; drawing (the gfx lock held, a clip armed)
; -----------------------------------------------------------------------------
; sc_drawchoose - the chooser card in place of Checks and Findings
sc_drawchoose:
    call sc_pen
    mov ax, [sc_geo+SCG_KX1]
    mov bx, [sc_geo+SCG_PY2]
    add bx, 4
    mov cx, [sc_geo+SCG_KX2]
    mov dx, [sc_geo+SCG_BTY]
    sub dx, 4
    call OSAPI_GFX_FRAME
    mov dx, [sc_geo+SCG_PY2]
    add dx, 4 + 5
    mov cx, [sc_geo+SCG_KTX]
    mov si, sc_h_choose
    call sc_vcells                  ; BX = the row's cells
    mov ax, SC_INK
    call sc_run_at
    xor ax, ax
.r:
    call sc_drawvrow
    inc ax
    cmp al, [sc_nvol]
    jb .r
    ret

; sc_vcells - BX = the chooser's text cells (keeps the rest)
sc_vcells:
    mov bx, [sc_geo+SCG_KX2]
    sub bx, [sc_geo+SCG_KTX]
    sub bx, 8
    shr bx, 1
    shr bx, 1
    shr bx, 1
    ret

; sc_drawvrow - chooser row AX ("  C:  hard disk, the boot volume"); keeps AX
sc_drawvrow:
    push ax
    cmp ax, [sc_geo+SCG_VFIT]
    jae .out                        ; past the card's foot: not drawn
    mov si, ax
    mov bx, ax
    mov ax, [sc_geo+SCG_LP]
    mul bx
    add ax, [sc_geo+SCG_VY0]
    push ax                         ; the y
    mov di, sc_txt
    mov byte [di], ' '
    mov byte [di+1], ' '
    inc di
    inc di
    mov al, [sc_vols+si]
    add al, 'A'
    mov [di], al
    mov byte [di+1], ':'
    mov byte [di+2], ' '
    mov byte [di+3], ' '
    add di, 4
    push si
    mov bx, sc_v_floppy
    cmp byte [sc_vkind+si], VK_FIXED
    jne .k
    mov bx, sc_v_hd
    cmp byte [sc_vtran+si], VT_FILE
    jne .k
    mov bx, sc_v_link
.k:
    mov si, bx
    call sc_scpy
    pop si
    mov ax, si
    cmp al, [sc_vboot]
    jne .t
    push si
    mov si, sc_v_boot
    call sc_scpy
    pop si
.t:
    pop dx                          ; DX = y
    mov ax, SC_INK
    mov bx, si
    cmp bl, [sc_vsel]
    jne .c
    mov ax, SC_INV
.c:
    mov cx, [sc_geo+SCG_KTX]
    call sc_vcells
    mov si, sc_txt
    call sc_run_at
.out:
    pop ax
    ret

; sc_hitvol - AL = the chooser row under CX,DX (screen), CF = 1 none
sc_hitvol:
    push bx
    push cx
    push dx
    cmp byte [sc_laid], 0
    je .no
    cmp cx, [sc_geo+SCG_KX1]
    jb .no
    cmp cx, [sc_geo+SCG_KX2]
    ja .no
    mov ax, dx
    sub ax, [sc_geo+SCG_VY0]
    jb .no
    xor dx, dx
    div word [sc_geo+SCG_LP]
    cmp ax, [sc_geo+SCG_VFIT]
    jae .no
    cmp al, [sc_nvol]
    jae .no
    clc
    jmp short .out
.no:
    stc
.out:
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; the chooser's gestures (SI = window, gfx lock held; each keeps SI)
; -----------------------------------------------------------------------------
; sc_choose_open - Scan, when no scan is running: ask again
sc_choose_open:
    call sc_volscan
    cmp byte [sc_ended], 0          ; after a scan, offer the drive it checked
    je .up
    xor bx, bx
.f:
    cmp bl, [sc_nvol]
    jae .up
    mov al, [sc_vols+bx]
    cmp al, [sc_vol]
    je .hit
    inc bx
    jmp short .f
.hit:
    mov [sc_vsel], bl
.up:
    mov byte [sc_choose], 1
    jmp sc_repaint

; sc_choose_go - check the selected drive
sc_choose_go:
    mov bl, [sc_vsel]
    xor bh, bh
    mov al, [sc_vols+bx]
    mov [sc_vol], al
    mov byte [sc_choose], 0
    call sc_scan_begin
    call sc_repaint
    jmp sc_kick

; sc_choose_esc - Esc puts the last scan's results back, if there was one
sc_choose_esc:
    cmp byte [sc_ended], 0
    je .out
    mov byte [sc_choose], 0
    jmp sc_repaint
.out:
    ret

; sc_vmove - select chooser row AL (from a key or a click)
sc_vmove:
    cmp al, [sc_nvol]
    jae .out
    mov ah, [sc_vsel]
    cmp al, ah
    je .out
    mov [sc_vsel], al
    push si
    push ax
    mov bx, si
    call OSAPI_WM_CLIP_SET
    pop ax
    jc .done
    call sc_pen
    push ax
    mov al, ah
    xor ah, ah
    call sc_drawvrow                ; the old row, plain...
    pop ax
    xor ah, ah
    call sc_drawvrow                ; ...the new one, inverse
.done:
    pop si
.out:
    ret

; sc_vclick - a press on the chooser: select, and a second press on the same
;             row inside SC_DBLT ticks checks it
; in:  CX, DX = the press; out: CF = 1 the press was the chooser's
sc_vclick:
    call sc_hitvol
    jc .no
    push ax
    call OSAPI_GET_TICKS
    mov dx, ax
    sub dx, [sc_vclkt]
    mov [sc_vclkt], ax
    pop ax
    cmp al, [sc_vsel]
    jne .sel
    cmp dx, SC_DBLT
    ja .yes
    call sc_choose_go
    stc
    ret
.sel:
    call sc_vmove
.yes:
    stc
    ret
.no:
    clc
    ret

sc_h_choose: db 'Choose the drive to check, then Scan', 0
sc_v_floppy: db 'floppy', 0
sc_v_hd:     db 'hard disk', 0
sc_v_link:   db 'network or RAM disk', 0
sc_v_boot:   db ', the boot volume', 0
