%ifdef KERN_BIG
; =============================================================================
; DESKTOP.DRV: bounded DESKTOP.CFG I/O (SPEC.md 26.8). Big/emu only.
; Loaded after the desktop is ready. DS remains the kernel; module constants are reached through CS.
; =============================================================================
section .modt
dl_badchars: db '"*+,/:;<=>?[|]',0
; Read into a separate claim, validate before publishing any live records.
dlt_load:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call COLD_SEG:drvf_drv_vol_bank
    call COLD_SEG:drvf_drv_mounted
    jc .fail
    mov si, dl_cfgname
    call COLD_SEG:dwf_dskw_stat
    jc .missing
    call dl_claim
    jc .fail
    mov es, [dl_seg]
    xor bx, bx
    mov cx, DL_CAP
    xor dx, dx
    mov si, dl_cfgname
    call COLD_SEG:dwf_dskw_read
    jc .invalid
    or dx, dx
    jnz .invalid
    cmp ax, DL_HDR
    jb .invalid
    mov bx, ax
    cmp word [es:0], 0x384F       ; O8
    jne .invalid
    cmp word [es:2], 0x4438       ; 8D
    jne .invalid
    cmp word [es:4], 0x5345       ; ES
    jne .invalid
    cmp word [es:6], 0x004B       ; K NUL
    jne .invalid
    cmp word [es:8], 1
    jne .invalid
    cmp word [es:10], DL_REC
    jne .invalid
    mov cx, [es:12]
    cmp cx, DL_MAX
    ja .invalid
    mov ax, DL_REC
    mul cx
    add ax, DL_HDR
    cmp ax, bx
    jne .invalid
    mov di, DL_HDR
    or cx, cx
    jnz .record
    jmp .publish
.record:
    cmp byte [es:di+DL_DRV], DVOL_MAX
    jae .invalid
    cmp byte [es:di+DL_TYPE], 2
    ja .invalid
    cmp byte [es:di+DL_PATH], '\'
    jne .invalid
    push cx
    push di
    add di, DL_PATH+1
    mov cx, DL_PATHMAX-1
    xor bx, bx                  ; stem <= 8, extension <= 3
    xor dx, dx                  ; nonzero after the component's dot
.path:
    mov al, [es:di]
    inc di
    or al, al
    jz .endpath
    cmp al, '\'
    je .separator
    cmp al, 0x21
    jb .badpath
    cmp al, 0x7E
    ja .badpath
    cmp al, '.'
    je .dot
    mov si, dl_badchars
.forbidden:
    cmp al, [cs:si]
    je .badpath
    cmp byte [cs:si], 0
    je .character
    inc si
    jmp short .forbidden
.character:
    inc bx
    cmp bx, 8
    ja .badpath
    or dx, dx
    jz .nextchar
    cmp bx, 3
    ja .badpath
.nextchar:
    loop .path
    jmp short .badpath
.dot:
    or dx, dx
    jnz .badpath
    or bx, bx
    jz .badpath
    inc dx
    xor bx, bx
    loop .path
.badpath:
    pop di
    pop cx
    jmp .invalid
.separator:
    or bx, bx
    jz .badpath
    xor bx, bx
    xor dx, dx
    loop .path
    jmp short .badpath
.endpath:
    or bx, bx
    jz .badpath
    pop di
    pop cx
    add di, DL_REC
    dec cx
    jz .publish
    jmp .record
.publish:
    mov ax, [es:12]
    mov [dl_count], al
    or al, al
    jnz .loaded
    call dl_release
.loaded:
    clc
    jmp short .back
.missing:
    cmp ax, FERR_NOENT
    jne .fail
    clc
    jmp short .back
.invalid:
    ; A foreign/torn file is not a partial live desktop.
    mov byte [dl_count], 0
    call dl_release
.fail:
    mov ax, FERR_NAME
    stc
.back:
    call COLD_SEG:drvf_drv_vol_back
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    retf

; Kernel-only system write. Never writes onto a swapped-in data disk.
dlt_save:
    call dlt_save_near
    retf
dlt_save_near:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call COLD_SEG:drvf_drv_vol_bank
    call COLD_SEG:drvf_drv_mounted
    jc .nodisk
    mov si, dl_sysname
    call COLD_SEG:dwf_dskw_stat
    jc .notsys
    mov es, [dl_seg]
    xor di, di
    xor ax, ax
    mov cx, DL_HDR/2
    cld
    rep stosw
    mov word [es:0], 0x384F
    mov word [es:2], 0x4438
    mov word [es:4], 0x5345
    mov word [es:6], 0x004B
    mov word [es:8], 1
    mov word [es:10], DL_REC
    xor ax, ax
    mov al, [dl_count]
    mov [es:12], ax
    mov cl, 8
    shl ax, cl
    add ax, DL_HDR
    mov cx, ax
    xor dx, dx
    xor bx, bx
    mov si, dl_cfgname
    call COLD_SEG:dwf_dskw_write_sys
    jmp short .back
.notsys:
    mov ax, FERR_NOENT
    stc
    jmp short .back
.nodisk:
    mov ax, FERR_NODISK
    stc
.back:
    call COLD_SEG:drvf_drv_vol_back
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

section .text
%endif
