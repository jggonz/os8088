; =============================================================================
; os8088 - apps/pixel/pxedit.inc
;
; EDITING, THE RESIDENT HALF (SPEC.md 106.24): what Image and Effects do
; between a menu item and the EDIT part (apps/pixel/pxedit.asm, part 7) -
; the claims an operation needs, the job the worker runs, its commit, and
; UNDO. The arithmetic is all the part's; this file decides, claims, and
; says what happened.
;
; TWO KINDS OF OPERATION. A PALETTE operation edits [px_pal] on the UI task
; through the part and is shown at once: the plans of the new palette made
; (the picture's mode becomes PAL - an edited cube is not the cube, so its
; shipped plans no longer describe it), the tables, the canvas rendered,
; the histogram folded again. A PIXEL operation runs on the WORKER (JOB_EDIT)
; with the canvas still showing the picture as it was, and is committed on
; the UI task when the worker answers - or rolled back by Esc.
;
; ONE LEVEL OF UNDO, which is also redo where it costs nothing:
;   UK_PAL     the other palette (and its plans, banked like an open's:
;              px_ppal / px_pplan) - an exchange, so Undo becomes Redo
;   UK_FLIP    a flip or the half turn done in place: done again
;   UK_MASTER  the old master kept in its claim - an exchange of the two
;   UK_XMS     the old master copied above 1 MB before an operation done in
;              place, on a 286+: copied back, and no redo
; An operation that needs a second master greys its menu item with the
; fact when the heap says no (px_eroom, SPEC.md 47), and an operation that
; could run in place without a copy behind it is not offered at all.
; =============================================================================

; (JOB_EDIT, JOB_SAVE, PXB_*, OC_* and UK_*: pxed.inc - the parts read them)

; ...and its name, for Edit > Undo
px_onm:     dw px_on_inv, px_on_grey, px_on_sepia, px_on_bc, px_on_gam
            dw px_on_post, px_on_thr, px_on_lev, px_on_rcw, px_on_rccw
            dw px_on_r180, px_on_fh, px_on_fv, px_on_crop, px_on_res
            dw px_on_blur, px_on_sharp, px_on_edge, px_on_emb, px_on_pix
px_on_inv:  db 'Invert', 0
px_on_grey: db 'Greyscale', 0
px_on_sepia: db 'Sepia', 0
px_on_bc:   db 'Brightness', 0
px_on_gam:  db 'Gamma', 0
px_on_post: db 'Posterize', 0
px_on_thr:  db 'Threshold', 0
px_on_lev:  db 'Auto Levels', 0
px_on_rcw:  db 'Rotate CW', 0
px_on_rccw: db 'Rotate CCW', 0
px_on_r180: db 'Rotate 180', 0
px_on_fh:   db 'Flip Across', 0
px_on_fv:   db 'Flip Down', 0
px_on_crop: db 'Crop', 0
px_on_res:  db 'Resize', 0
px_on_blur: db 'Blur', 0
px_on_sharp: db 'Sharpen', 0
px_on_edge: db 'Edge Detect', 0
px_on_emb:  db 'Emboss', 0
px_on_pix:  db 'Pixelate', 0
px_s_undo:  db MENU_DIS, 'Undo ', 0
px_s_redo:  db MENU_DIS, 'Redo ', 0
px_s_working: db 'Working ', 0
px_s_saving: db 'Saving ', 0

; =============================================================================
; THE ENTRY: a verb from a menu, a tool or a card - the EDIT part's flow
; (EV_UPAL, EV_USTART, apps/pixel/pxedit.asm) once nothing stands in its way
; =============================================================================

; px_edo - AL = a verb (its parameters in [px_ep]): done, or refused in a
; toast. UI task, lock held. Preserves all
px_edo:
    push ax
    push cx
    push si
    mov si, px_s_nopico
    cmp byte [px_cur + PXR_HAVE], 0
    je .say
    mov si, px_s_busy
    cmp byte [px_busy], 0
    jne .say
    xor ah, ah
    mov cl, EV_UPAL
    cmp al, EV_NPAL
    jb .go
    mov cl, EV_USTART
.go:
    call px_ecall
    jmp short .x
.say:
    call px_toast
.x:
    pop si
    pop cx
    pop ax
    ret

; px_eshow - the picture changed in its palette or its pixels: a new key for
; the Navigator, the histogram folded again, the tables for this palette
; made now (the old picture stays on the glass until they are), the window
; told, and the canvas rendered. Lock held. Preserves all
px_eshow:
    push ax
    inc byte [px_cur + PXR_SERIAL]
    mov byte [px_thok], 0
    mov byte [px_tabok], 0
    call px_hstats
    mov byte [px_hok], 1
    ; A PALETTE'S PLANS ON A 4BPP DISPLAY ARE A SEARCH - 2.28 s on an XT
    ; (106.17) - so the WORKER builds them (JOB_TABLES, [px_busy] 2) and the
    ; picture shown stays until px_finish draws the new one; a threshold
    ; (1bpp) or the cube's and grey's shipped plans are built here
    cmp byte [px_bpp], 1
    jbe .sync
    cmp byte [px_cur + PXR_PMODE], PM_PAL
    jne .sync
    cmp byte [px_pvalid], 0         ; (a turn or a crop keeps the palette,
    jne .sync                       ; and its plans with it)
    cmp byte [px_busy], 0
    jne .sync
    call px_viewclaim
    jc .sync
    mov al, [px_cur + PXR_FMT]      ; THE PART THE PLANS ARE MADE BY, here:
    call px_fpart                   ; a save gave it back, and a worker
    or al, al                       ; cannot fetch - the search fell to the
    jz .sync                        ; UI task again (review-w7 F14)
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov [px_ppart], al
    call px_pfetch
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    jc .sync
    mov al, [px_ppart]
    call px_kneed                   ; (held from now, as a decode's is)
    call px_spawn
    jc .sync
    mov al, [px_bpp]
    mov [px_tdepth], al
    mov byte [px_abort], 0
    mov byte [px_busy], 2
    mov byte [px_job], JOB_TABLES   ; LAST
    call px_compose
    call px_flags
    call px_eseltrim
    mov al, PX_R_STATUS
    call px_regdraw
    pop ax
    ret
.sync:
    call px_tabsync
    call px_compose
    call px_flags
    call px_eseltrim
    mov al, PX_R_CANVAS | PX_R_PANELS | PX_R_STATUS
    call px_regdraw                 ; (the render puts the selection's
    pop ax                          ; outline back, px_rcanvas)
    ret

; px_eprog - the name field says "Working 42%" while an operation runs, or
; "Saving 42%" while a save does: [px_erow] over [px_erows]. Preserves all
px_eprog:
    push ax
    push bx
    push dx
    push si
    push di
    mov ax, [px_erow]
    mov bx, 100
    mul bx
    mov bx, [px_erows]
    or bx, bx
    jz .out
    div bx
    cmp ax, 99
    jbe .p
    mov ax, 99
.p:
    mov si, px_s_working
    cmp byte [px_busy], PXB_SAVE
    jne .s
    mov si, px_s_saving
.s:
    mov di, px_cline
    call px_strcpy
    xor dx, dx
    call px_u32n
    mov si, px_s_pct
    call px_strcat
    mov bx, PX_SF_NAME
    mov si, px_cline
    call px_sval
.out:
    pop di
    pop si
    pop dx
    pop bx
    pop ax
    ret

; px_efin - the worker has answered an operation (UI task, lock held): the
; EDIT part's commit, or its roll back (EV_UFIN)
px_efin:
    push ax
    push cx
    mov byte [px_busy], 0
    mov cl, EV_UFIN
    call px_ecall
    pop cx
    pop ax
    ret

; px_eundo - Edit > Undo (Ctrl+Z): the EDIT part's (EV_UUNDO). Lock held
px_eundo:
    push ax
    push cx
    push si
    mov si, px_s_busy
    cmp byte [px_busy], 0
    jne .say
    mov cl, EV_UUNDO
    call px_ecall
    jmp short .x
.say:
    call px_toast
.x:
    pop si
    pop cx
    pop ax
    ret

; px_ufree - undo forgets: an old master's claim or an XMS copy given back.
; Preserves all
px_ufree:
    push ax
    push dx
    mov al, [px_ukind]
    mov byte [px_ukind], UK_NONE
    cmp al, UK_MASTER
    jne .x
    mov dx, [px_urec + PXR_MSEG]
    call OSAPI_MEM_FREE
.x:
    cmp al, UK_XMS
    jne .o
    mov ax, [px_uxms]
    mov dx, [px_uxms + 2]
    call OSAPI_XMEM_FREE
.o:
    pop dx
    pop ax
    ret

; px_eclear - an open is leaving the picture: undo forgotten, the selection
; gone, the EDIT part dropped. Preserves all
px_eclear:
    call px_ufree
    call px_seloff
    push ax
    mov al, PXPART_EDIT
    call px_pdrop
    pop ax
    ret

; =============================================================================
; WHAT THE MENUS OFFER (SPEC.md 47: grey a fact)
; =============================================================================

; px_eroom - CF = 0 when the heap has a run for the destination an
; operation of class AL needs at the shown master's size (a quarter turn,
; a crop to all, a kernel, Pixelate: the same pixels), or - OC_EITHER - the
; store above 1 MB has room for the copy that makes it in place. Preserves
; all
px_eroom:
    push ax
    push bx
    push dx
    cmp al, OC_DEST
    jb .yes
    mov bl, al
    call px_mkbnow                  ; AX = the master's claim in KB
    mov dx, ax
    call OSAPI_MEM_AVAIL            ; AX = the largest run - and, when undo
    cmp byte [px_ukind], UK_MASTER  ; holds a master, its claim, which the
    jne .r                          ; operation frees before it claims
    add ax, [px_urec + PXR_MKB]     ; (an upper bound: review-w7 F12)
.r:
    cmp ax, dx
    jae .yes
    cmp bl, OC_EITHER
    jne .no
    call OSAPI_XMEM_CAPS
    cmp ax, dx
    jae .yes
.no:
    stc
    jmp short .x
.yes:
    clc
.x:
    pop dx
    pop bx
    pop ax
    ret

; px_mkbnow - AX = the shown master's claim, KB, at its size now
px_mkbnow:
    push dx
    mov ax, [px_cur + PXR_MW]
    mul word [px_cur + PXR_MH]
    add ax, 1023
    adc dx, 0
    mov al, ah
    mov ah, dl
    shr ax, 1
    shr ax, 1
    add ax, PX_HTAILKB
    pop dx
    ret

; px_emenu - the Edit, Image and Effects items that follow the picture and
; the heap, and Undo's label. CX counts what changed (px_menulive's
; convention). Preserves all but CX
px_emenu:
    push ax
    push bx
    push dx
    push si
    push di
    xor dx, dx                      ; DX = 1: a picture to edit, idle
    cmp byte [px_cur + PXR_HAVE], 0
    je .g
    cmp byte [px_busy], 0
    jne .g
    inc dx
.g:
    mov si, px_medit                ; every item: its slot, its string, its
.l:                                 ; class
    mov bx, [si]
    or bx, bx
    jz .undo
    mov ax, [si + 2]
    push dx
    or dx, dx
    jz .s
    mov al, [si + 4]
    call px_eroom
    jnc .s1
    xor dx, dx
.s1:
    mov ax, [si + 2]
.s:
    add ax, dx
    pop dx
    cmp [bx], ax
    je .n
    mov [bx], ax
    inc cx
.n:
    add si, 5
    jmp short .l
.undo:
    ; Deselect and Crop to Selection: a selection as well
    mov ax, dx
    cmp byte [px_sel], 0
    jne .sl
    xor ax, ax
.sl:
    mov bx, px_mie + 2 * PX_ME_DESEL
    mov si, px_mi_desel
    call .set
    mov bx, px_mie + 2 * PX_ME_CROP
    mov si, px_mi_crop
    call .set
    ; UNDO's label: "Undo Blur" or "Redo Blur", made again only when what
    ; it names changed; live when undo holds something and nothing runs
    mov al, [px_uop]
    mov ah, [px_uredo]
    ror ah, 1
    or al, ah
    cmp byte [px_ukind], UK_NONE
    jne .k
    mov al, 0x7F
.k:
    cmp al, [px_ulkey]
    je .live
    mov [px_ulkey], al
    inc cx                          ; (new words in the same slot)
    mov si, px_s_undo
    cmp byte [px_uredo], 0
    je .u
    mov si, px_s_redo
.u:
    mov di, px_mundo
    call px_strcpy
    cmp al, 0x7F
    je .live
    mov bl, [px_uop]
    xor bh, bh
    shl bx, 1
    mov si, [px_onm + bx]
    call px_strcat
.live:
    mov ax, px_mi_undo
    cmp byte [px_ukind], UK_NONE
    je .ul
    mov ax, px_mundo
    cmp byte [px_busy], 0
    jne .ul
    inc ax
.ul:
    cmp [px_mie + 2 * PX_ME_UNDO], ax
    je .x
    mov [px_mie + 2 * PX_ME_UNDO], ax
    inc cx
.x:
    pop di
    pop si
    pop dx
    pop bx
    pop ax
    ret
; .set - BX = an item's slot, SI = its string (MENU_DIS first), AX = 1
; live, 0 grey: CX counts a change. Preserves all but SI
.set:
    add si, ax
    cmp [bx], si
    je .sr
    mov [bx], si
    inc cx
.sr:
    ret

px_medit:                           ; slot, string, class
    dw px_mif + 2 * PX_MF_SAVEAS, px_mi_saveas
    db OC_PAL
    dw px_mie + 2 * PX_ME_COPY, px_mi_copy
    db OC_PAL
    dw px_mie + 2 * PX_ME_SELALL, px_mi_selall
    db OC_PAL
    dw px_mii + 0, px_mi_rcw
    db OC_DEST
    dw px_mii + 2, px_mi_rccw
    db OC_DEST
    dw px_mii + 4, px_mi_r180
    db OC_FLIP
    dw px_mii + 6, px_mi_fliph
    db OC_FLIP
    dw px_mii + 8, px_mi_flipv
    db OC_FLIP
    dw px_mii + 12, px_mi_resize
    db OC_PAL                       ; (its card says what memory allows)
    dw px_mii + 14, px_mi_alev
    db OC_PAL
    dw px_mii + 16, px_mi_bc
    db OC_PAL
    dw px_mii + 18, px_mi_grey
    db OC_PAL
    dw px_mii + 20, px_mi_inv
    db OC_PAL
    dw px_mix + 0, px_mi_blur
    db OC_EITHER
    dw px_mix + 2, px_mi_sharp
    db OC_EITHER
    dw px_mix + 4, px_mi_edge
    db OC_EITHER
    dw px_mix + 6, px_mi_emb
    db OC_EITHER
    dw px_mix + 8, px_mi_pixl
    db OC_EITHER
    dw px_mix + 12, px_mi_sepia
    db OC_PAL
    dw px_mix + 14, px_mi_post
    db OC_PAL
    dw px_mix + 16, px_mi_thr
    db OC_PAL
    dw px_mix + 18, px_mi_gam
    db OC_PAL
    dw 0

; =============================================================================
; THE MENUS' COMMANDS: Edit, Image, Effects (SPEC.md 106.2, 106.24)
; =============================================================================

; px_edcmd - AH = a menu, AL = its item: CF = 0 it was editing's and has
; been done. Lock held. Preserves all
px_edcmd:
    push ax
    push bx
    cmp ah, PX_M_EDIT
    jne .im
    cmp al, PX_ME_UNDO
    jne .e1
    call px_eundo
    jmp short .y
.e1:
    cmp al, PX_ME_COPY
    jne .e2
    call px_copy
    jmp short .y
.e2:
    cmp al, PX_ME_SELALL
    jne .e3
    call px_selall
    jmp short .y
.e3:
    cmp al, PX_ME_DESEL
    jne .e4
    call px_seloff
    call px_menulive
    jmp short .y
.e4:
    cmp al, PX_ME_CROP
    jne .y
    call px_crop
    jmp short .y
.im:
    mov bx, px_mmim
    cmp ah, PX_M_IMAGE
    je .map
    mov bx, px_mmfx
    cmp ah, PX_M_FX
    jne .no
.map:
    xor ah, ah
    add bx, ax
    mov al, [bx]
    test al, 0x80
    jz .op
    and al, 0x7F                    ; a card first
    call px_pcopen
    jmp short .y
.op:
    call px_edo
.y:
    pop bx
    pop ax
    clc
    ret
.no:
    pop bx
    pop ax
    stc
    ret

; the items' verbs, in the menus' order (0FFh a separator; | 80h a card)
px_mmim:    db EV_ROTCW, EV_ROTCCW, EV_ROT180, EV_FLIPH, EV_FLIPV, 0xFF
            db PC_RES | 0x80, EV_LEVELS, PC_BC | 0x80, EV_GREY, EV_INVERT
px_mmfx:    db EV_BLUR, EV_SHARP, EV_EDGE, EV_EMBOSS, EV_PIXEL, 0xFF
            db EV_SEPIA, PC_POST | 0x80, PC_THR | 0x80, PC_GAM | 0x80
