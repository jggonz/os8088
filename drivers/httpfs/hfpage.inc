; =============================================================================
; hfpage.inc - HTTPFS.DRV's half of its Control Panel page, and its remembered
; address (SPEC.md 122.5)
;
; THE PAGE IS HTTPPAGE.DRV (httppage.asm), RAMPAGE.DRV's shape (SPEC.md
; 62.9.9): read off the boot volume's root by the first PAINT, freed when the
; panel closes, so a drive that is up costs nothing for a page nobody is
; looking at. What stays here is what has to: the six thunks the kernel calls,
; the loader they share, the one line a paint draws when the image will not
; come in, and HTTPDRV.CFG - read at DRVV_READY, before any panel exists, and
; written by the door's HFV_SAVE, which the page sends at its close.
;
; THE PAGE REACHES US THROUGH THE PACKAGE DOOR (hfpkg.inc), OSAPI_DRV_CALL
; like an installer's, so the only ABI between the two images is the HPT_*
; verb in AL and the header's HFP_ABI.
; =============================================================================

; -----------------------------------------------------------------------------
; The six thunks: AL = the HPT_*, every other register the kernel's
; -----------------------------------------------------------------------------
hf_cp_paint:
    push ax
    call hp_need                ; CF = 1: it will not come in
    mov al, HPT_PAINT
    jnc hp_fwd
    push cx                     ; THE REFUSAL IS DRAWN AND NOT SWALLOWED: the
    push dx                     ; kernel has white-filled the pane and will
    push si                     ; not come back (rd_page_paint's reason)
    mov cx, di
    add cx, 2
    add dx, 4
    mov si, hf_s_nopg
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    pop si
    pop dx
    pop cx
    pop ax
    ret
hf_cp_key:                      ; ...none of these LOADS: a paint precedes
    push ax                     ; anything else on the page, so with no image
    mov al, HPT_KEY             ; it is the one-line refusal they land on
    jmp short hp_fwd
hf_cp_up:
    push ax
    mov al, HPT_UP
    jmp short hp_fwd
hf_cp_drag:
    push ax
    mov al, HPT_DRAG
    jmp short hp_fwd
hf_cp_click:
    push ax
    mov al, HPT_CLICK
hp_fwd:                         ; AL = the verb, the caller's AX on the stack
    call hp_call
    pop ax
    ret

; --- hp_call - AL = an HPT_*, to the image. out: its CF; CF=1 = no image ----
hp_call:
    cmp word [hf_pseg], 0
    stc                         ; no image: CF = 1, a key "not taken"
    je .out
    push ds
    push bp
    mov bp, [hf_pent]           ; the dispatcher is `call bp`: an ADDRESS
    mov ds, [hf_pseg]
    call far [cs:hf_pfar]
    pop bp
    pop ds
.out:
    ret

; --- DSV_CPCLOSE: the page writes what it owes, then its memory goes --------
hf_cp_close:
    push ax
    mov al, HPT_CLOSE
    call hp_call
    pop ax
hp_drop:                        ; ...and DRVV_DETACH's, which owes nothing
    push dx
    xor dx, dx
    xchg dx, [hf_pseg]
    or dx, dx
    jz .out
    call OSAPI_MEM_FREE
.out:
    pop dx
    ret

; -----------------------------------------------------------------------------
; hp_need - the page image, in memory. out: CF=0 here; CF=1 it is not.
; preserves every register
;
; Off the BOOT volume's root, where HTTPDRV.CFG is and where the drivers are,
; by hp_root's quiet goto. HFPAGE_KB is the Makefile's, off httppage.bin -
; the claim a CEILING, as RAMPAGE_KB is: OSAPI_FILE_READ is the transparent
; read, so a 'CZ' file arrives as the image (SPEC.md 20.13.5.1). From the TOP
; (SPEC.md 50.3.2.1): its base is a CS, so no compaction may have it
; -----------------------------------------------------------------------------
hp_need:
    cmp word [hf_pseg], 1       ; CF = 0: it is in already
    jnc .ret
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call hp_root
    jc .back
    mov ax, HFPAGE_KB
    call OSAPI_MEM_CLAIM_HI
    jc .back
    mov [hf_pseg], dx
    mov es, dx
    xor di, di                  ; ZEROED FIRST: the page's state is the zeros
    mov cx, HFPAGE_KB * 512     ; os88drv strips off the FILE, which drv_bss
    xor ax, ax                  ; puts back for a driver the kernel loads and
    cld
    rep stosw                   ; nothing puts back for this one - a warm boot
                                ; left 55h there and the page read it as an
                                ; address it already had (rampage.asm's rp_bss
                                ; is the same answer, from the other side)
    xor bx, bx
    mov cx, HFPAGE_KB * 1024
    xor dx, dx
    mov si, hf_s_page
    call OSAPI_FILE_READ
    jc .drop
    cmp word [es:0], 0x384F     ; 'O8', an overlay of ours...
    jne .drop
    cmp byte [es:10], HFP_ABI   ; ...built against this table of verbs
    jne .drop
    mov ax, [es:6]
    mov [hf_pent], ax
    clc
    jmp short .back
.drop:
    call hp_drop
    stc
.back:
    call hp_vback
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.ret:
    ret

; -----------------------------------------------------------------------------
; HFV_SAVE - HTTPDRV.CFG from the address and whether a drive is up. SILENT
; about failing to its caller's user: the page sends it at the panel's close
; -----------------------------------------------------------------------------
hf_cfg_save:
    mov word [hf_cfg], HF_CFMAGIC
    mov byte [hf_cfg+2], HF_CFVER
    xor al, al
    cmp byte [hf_vol], 0xFF
    je .fl
    inc ax                      ; bit 0: mount it at the next boot
.fl:
    mov [hf_cfg+3], al
    call hp_fargs
    jc .back
    call OSAPI_FILE_WRITE
.back:
    jmp hp_vback

; -----------------------------------------------------------------------------
; hf_cfg_load - HTTPDRV.CFG, if there is one (DRVV_READY). out: CF=0 and the
;               address is in [hf_url], AL = its flags; CF=1 none
; -----------------------------------------------------------------------------
hf_cfg_load:
    call hp_fargs
    jc .back
    call OSAPI_FILE_READ
    jc .back
    cmp word [hf_cfg], HF_CFMAGIC
    jne .no
    cmp byte [hf_cfg+2], HF_CFVER
    jne .no
    mov byte [hf_url + HF_URLMAX - 1], 0    ; hostile: terminated, whatever
    mov al, [hf_cfg+3]
    jmp short .back             ; CF = 0 from the compare
.no:
    stc
.back:
    jnc hp_vback
    mov byte [hf_url], 0        ; a refused read may have left half of it
    jmp hp_vback

; --- hp_fargs - hp_root, then a read or write of HTTPDRV.CFG: ES:BX = the
;     record, SI = the name, DX:CX = its size. CF=1 = nothing was moved
hp_fargs:
    call hp_root
    push ds
    pop es
    mov si, hp_s_file
    mov bx, hf_cfg
    mov cx, HF_CFSIZE
    mov dx, 0                   ; NOT xor: CF is the answer
    ret

; --- hp_root - stand on the boot volume's root, QUIETLY (ethcfg.inc's rule),
;     having banked where we were for hp_vback. CF=1: nothing moved
hp_root:
    call OSAPI_FILE_HERE        ; DX = our cluster, BL = our drive
    mov [hf_bdrv], bl
    mov [hf_bclus], dx
    call OSAPI_VOL_SYS          ; BL = the volume we booted from
    xor dx, dx
    call OSAPI_FILE_GOTO_Q
    ret

; --- hp_vback - back where hp_root found us. preserves AX, BX, DX, flags ---
hp_vback:
    push ax
    push bx
    push dx
    pushf
    mov bl, [hf_bdrv]
    mov dx, [hf_bclus]
    call OSAPI_FILE_GOTO_Q
    popf
    pop dx
    pop bx
    pop ax
    ret

HF_CFMAGIC  equ 'HD'
HF_CFVER    equ 1
HF_CFSIZE   equ 4 + HF_URLMAX   ; magic, version, flags, then the address

hf_pfar:    dw PKG_DISP         ; the page's dispatcher, as a far pointer -
hf_pseg:    dw 0                ; whose segment IS the page's, 0 = not loaded
hf_pent:    dw 0                ; ...and its entry, out of its header's +6
hp_s_file:  db 'HTTPDRV.CFG', 0
hf_s_page:  db 'HTTPPAGE.DRV', 0
hf_s_nopg:  db 'Needs the system disk', 0
