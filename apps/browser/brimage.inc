; =============================================================================
; Browser 1.7 images, the RESIDENT half (SPEC.md 71.26)
;
; Part 1 (brimage_part.asm) fetches and decodes; what stays here is what must
; answer while it is not in memory: the table's filling at parse time, the
; thunks into the part, the engine's stop, and the PAINT - a page whose
; images are in needs no part to draw them.
; =============================================================================

; --- br_imgsub - DX = the store, AX = a paragraph offset into it -> DX = a
; segment based there: the sum on a real-mode kernel, OSAPI_SEG's SEG_SUB on
; kern_mega, where [br_istseg] is a selector. Preserves all but DX
br_imgsub:
    cmp byte [br_mega], 0
    jne .pm
    add dx, ax
    ret
.pm:
    push ax
    push bx
    push cx
    push es                         ; (a kernel call need not keep ES)
    mov bl, SEG_SUB
    xor cx, cx
    call OSAPI_SEG
    pop es
    pop cx
    pop bx
    pop ax
    ret

; --- br_imgent - BX = an image index -> BX = its table entry. Others kept
br_imgent:
    push cx
    mov cl, 4
    shl bx, cl
    add bx, br_itab
    pop cx
    ret

; --- br_arenacopy - the source span ES:SI..CX into the link arena -----------
; out: CF=0 AX = its offset there (NUL-terminated, control bytes dropped),
;      [br_lnkw] past it; CF=1 the arena refused (its cap, or memory). Others
;      kept. An href (br_anchor) and an image's src (br_imgtab) both land here
br_arenacopy:
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, cx
    sub bx, si                      ; BX = the value's length
    mov ax, [br_lnkw]
    add ax, bx
    inc ax
    call br_linkensure              ; demand-claim/grow only for a real value
    jc .out
    mov di, [br_lnkw]
    push di
    mov cx, bx
    mov dx, [br_lnkseg]
    mov ax, es                      ; the current logical source window; this
    push es                         ; may be the second 64 KiB alias
    push ds
    mov es, dx                      ; both ends are foreign segments
    mov ds, ax
    cld
.copy:
    jcxz .done
    lodsb
    cmp al, 0x20                    ; discard control bytes before storing URLs
    jb .skip
    stosb
.skip:
    loop .copy
.done:
    xor al, al
    stosb
    pop ds
    pop es
    mov [br_lnkw], di
    pop ax
    clc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; --- br_imgtab - fill the table entry of image AX (br_image, at parse) ------
; in:  AX = its INDEX; [br_iflg], [br_iw], [br_ih], [br_idoc]; the tag's
;      attribute span, ES = the source. All registers kept
br_imgtab:
    cmp ax, BR_IMGTMAX
    jae .ret
    push ax
    push bx
    push cx
    push si
    push di
    mov bx, ax
    call br_imgent
    mov byte [bx+IT_ST], IS_NONE
    mov word [bx+IT_SRC], 0FFFFh    ; no src until one is copied (0 is an
                                    ; arena offset): Save Image As refuses it
    mov al, [br_iflg]
    mov [bx+IT_FLG], al
    mov ax, [br_iw]
    mov [bx+IT_W], ax
    mov ax, [br_ih]
    mov [bx+IT_H], ax
    mov ax, [br_idoc]
    mov [bx+IT_DOC], ax
    mov ax, [br_idoc+2]
    mov [bx+IT_DOC+2], ax
    cmp byte [br_frameappend], 0    ; a frame child's: its base is not ours
    jne .out
    mov di, br_a_src
    call br_getattr
    jc .out
    cmp si, cx
    je .out                         ; src="" fetches nothing
    call br_arenacopy
    jc .out                         ; Links limited: the alt box stays
    mov [bx+IT_SRC], ax
    mov byte [bx+IT_ST], IS_WAIT
    mov byte [br_ipend], 1          ; the worker starts the engine after the page
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
.ret:
    ret

; --- br_imglaid - the layout drew image [br_iindex] as a box: its bit in
; [br_ilaid], which br_imgsetlaid makes IF_LAID at the walk's end. All kept
br_imglaid:
    push bx
    push cx
    mov bx, [br_iindex]
    cmp bx, BR_IMGTMAX
    jae .out
    mov cl, bl
    and cl, 7
    mov ch, 1
    shl ch, cl
    shr bx, 1
    shr bx, 1
    shr bx, 1
    or [br_ilaid + bx], ch
.out:
    pop cx
    pop bx
    ret

; --- br_imgunlaid - no box yet (br_layout's start). IF_LAID is untouched
; until the walk ends: the worker's IE_NEXT never sees a half-laid page
br_imgunlaid:
    mov word [br_ilaid], 0
    mov word [br_ilaid+2], 0
    ret

; --- br_imgsetlaid - every entry's IF_LAID := its bit in [br_ilaid], in one
; critical section, and [br_ilgen] bumped: bri_next's scan saw the old set
; or the new one, or a changed count tells it to look again. All kept
br_imgsetlaid:
    push ax
    push bx
    push cx
    push dx
    mov ax, [br_ilaid]
    mov dx, [br_ilaid+2]            ; DX:AX, bit k image k
    mov bx, br_itab + IT_FLG
    mov cx, BR_IMGTMAX
    pushf
    cli
.l:
    and byte [bx], ~IF_LAID & 0xFF
    shr dx, 1
    rcr ax, 1                       ; CF = this image's bit
    jnc .n
    or byte [bx], IF_LAID
.n:
    add bx, BR_ITSZ
    loop .l
    inc byte [br_ilgen]
    popf
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; THE ENGINE'S THUNKS (SPEC.md 71.26's table)
; =============================================================================

; br_im_step - the worker's pass, after br_nstep and br_dl_step. br_ibusy is
; stored BEFORE br_ie is read and the generation is banked before both, so a
; stop (br_imghalt) either sees the worker inside and waits, or the worker
; sees IE_OFF. No register need be kept (br_worker)
br_im_step:
    mov byte [br_ibusy], 1
    mov al, [br_igen]
    mov [br_igen0], al
    mov al, [br_ie]
    cmp al, IE_OFF
    jne .on
    cmp byte [br_ipend], 0          ; a page with an image to fetch...
    je .out
    mov al, [br_nstate]             ; ...that has settled...
    cmp al, BN_OPEN
    jb .settled
    cmp al, BN_DONE
    jb .out
.settled:
    cmp word [br_cols], 0           ; ...and been laid out
    je .out
    test byte [br_parse_started], BR_PARSE_LAYOUT
    jnz .out
    cmp byte [br_fin], 0
    jne .out
    mov byte [br_ipend], 0
    mov byte [br_ie], IE_START
    jmp short .kick
.on:
    cmp al, IE_DEC
    je .part
    cmp al, IE_NEXT
    jb .kick                        ; IE_START: the UI task's
    cmp al, IE_ASET
    je .part                        ; an animation's set-up and frames are
    cmp al, IE_ANIM                 ; the worker's (71.29)
    je .part
    cmp al, IE_GROW
    jae .kick                       ; GROW, PREP, SHOW, END: the UI task's
.part:
    cmp word [br_im_vec+2], 0
    je .out
    call far [br_im_vec+BRIM_STEP]
    jmp short .out
.kick:
    mov bx, [br_win]                ; posted on every pass until answered: a
    call OSAPI_WM_WAKE              ; wake into a full ring is refused
.out:
    mov byte [br_ibusy], 0
    ret

; br_im_wake - W_ONWAKE's share: the UI task's half of a hand-over. UI task,
; gfx lock not held; no register need be kept (br_onwake)
br_im_wake:
    mov al, [br_ie]
    cmp al, IE_START
    je .start
    cmp al, IE_END
    je .end
    cmp al, IE_GROW
    jb .ret
    cmp al, IE_DEC
    je .ret
.part:                              ; GROW, PREP, SHOW: the part's
    cmp word [br_im_vec+2], 0
    je .ret
    call far [br_im_vec+BRIM_WAKE]
.ret:
    ret
.start:
    call br_im_partload
    jc .refused
    call far [br_im_vec+BRIM_WAKE]  ; the store; IE_NEXT, or IE_END refused
    cmp byte [br_ie], IE_END
    je .end
    ret
.refused:
    call br_imgnone                 ; no part: every waiting image keeps its alt
.end:
    call br_im_partdrop
    mov byte [br_ie], IE_OFF
    ret

; br_imgnone - every waiting entry none. All kept
br_imgnone:
    push bx
    push cx
    mov bx, br_itab
    mov cx, BR_IMGTMAX
.l:
    cmp byte [bx+IT_ST], IS_WAIT
    jne .n
    mov byte [bx+IT_ST], IS_NONE
.n:
    add bx, BR_ITSZ
    loop .l
    pop cx
    pop bx
    ret

; br_imghalt - stop the engine and keep what it made: the close (it may be
; refused, and the page then still shows its images). UI task. All kept
; br_imgstop - ...and give everything back: every load, through br_free_all
br_imgstop:
    call br_imghalt
    push ax
    push dx
    mov dx, [br_istseg]
    or dx, dx
    jz .nostore
    mov word [br_istseg], 0
    call OSAPI_MEM_FREE
.nostore:
    xor ax, ax
    mov [br_istpar], ax
    mov [br_iready], ax
    mov [br_iemsp], ax              ; (br_docems_release frees br_imgar)
    mov [br_ibase], ax
    call br_icpush                  ; ...and the cache file goes, or is kept
    mov [br_ipend], al
    push bx                         ; every entry none, READY too: the store
                                    ; its copies were in is gone
    push cx
    mov bx, br_itab
    mov cx, BR_IMGTMAX
.all:
    mov byte [bx+IT_ST], IS_NONE
    add bx, BR_ITSZ
    loop .all
    pop cx
    pop bx
    pop dx
    pop ax
    ret

br_imghalt:
    push ax
    push bx
    inc byte [br_igen]              ; FIRST, and the order is the handshake
    mov byte [br_ie], IE_OFF
.wait:
    cmp byte [br_ibusy], 0
    je .idle
    call OSAPI_TASK_YIELD
    jmp short .wait
.idle:
    mov byte [br_ie], IE_OFF        ; a step that was inside may have moved it
    mov al, [br_ihnd]
    or al, al
    jz .nosock
    mov bh, NET_CLASS
    mov bl, NETV_CLOSE
    call OSAPI_DRV_CALL
    mov byte [br_ihnd], 0
.nosock:
    cmp word [br_im_vec+2], 0
    je .nopart
    call br_im_partdrop
.nopart:
    pop bx
    pop ax
    ret

; Fetch and bind part 1 on the UI task, as br_dl_partload binds part 0: its
; entry table's offsets become far vectors on its CODE selector, and its
; BRIM_SELF word learns its DATA one (pl_plan1's scratch is written through
; it). out CF=1 refused (no memory, or BROWSER.O88 gone). All kept
br_im_partload:
    push ax
    push cx
    push dx
    push si
    push di
    push es
    mov al, 1
    call op_fetch
    jc .bad
    mov al, 1
    call op_cseg
    or ax, ax
    jz .bad
    mov dx, ax
    mov al, 1
    call op_seg
    or ax, ax
    jz .bad
    mov es, ax
    mov [es:BRIM_SELF], ax
    xor si, si
    mov di, br_im_vec
    mov cx, BRIM_N
.vec:
    mov ax, [es:si]
    mov [di], ax
    mov [di+2], dx
    inc si
    inc si
    add di, 4
    loop .vec
    mov di, br_im_rtab
    mov cx, BRIM_RN
.back:
    mov [di+2], cs
    add di, 4
    loop .back
    clc
    jmp short .out
.bad:
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; Only where nothing can be inside the part: IE_END, or after br_imghalt.
; The decoder part part 1 hosts goes with it (71.30)
br_im_partdrop:
    push ax
    push cx
    push di
    push es
    mov al, 6                       ; the decoder parts, 6 down to 2...
.dp:
    call op_drop
    dec ax
    cmp al, 2
    jae .dp
    call op_drop                    ; ...and part 1 (AL = 1)
    push ds
    pop es
    mov di, br_im_vec
    mov cx, BRIM_N*2
    xor ax, ax
    cld
    rep stosw
    pop es
    pop di
    pop cx
    pop ax
    ret

; --- the primary routines part 1 calls: a far vector each, onto a shim that
; makes the near call and returns far (part 0's br_dl_rtab, whose shims are
; reused where they exist)
br_im_rtab:
br_im_r_net_find:   dw br_dl_rs_net_find, 0
br_im_r_docmap:     dw br_dl_rs_docmap, 0
br_im_r_paint_band: dw br_dl_rs_paint_band, 0
br_im_r_sbar:       dw br_dl_rs_sbar, 0
br_im_r_imgsub:     dw br_im_rs_imgsub, 0
br_im_r_claimp:     dw br_im_rs_claimp, 0
br_im_r_regrowp:    dw br_im_rs_regrowp, 0
br_im_r_memroom:    dw br_im_rs_memroom, 0
br_im_r_movable:    dw br_im_rs_movable, 0
br_im_r_layout:     dw br_im_rs_layout, 0
br_im_r_advance:    dw br_im_rs_advance, 0
br_im_r_emsframe:   dw br_im_rs_emsframe, 0
br_im_r_argrow:     dw br_im_rs_argrow, 0
br_im_r_armap:      dw br_im_rs_armap, 0
br_im_r_icenter:    dw br_im_rs_icenter, 0
br_im_r_odback:     dw br_im_rs_odback, 0
br_im_r_mkname:     dw br_im_rs_mkname, 0
br_im_r_icdel:      dw br_im_rs_icdel, 0
br_im_r_icdrop:     dw br_im_rs_icdrop, 0
br_im_r_pxload:     dw br_im_rs_pxload, 0
br_im_r_pxdrop:     dw br_im_rs_pxdrop, 0
br_im_r_pagepin:    dw br_dl_rs_pagepin, 0
br_im_r_pageunpin:  dw br_dl_rs_pageunpin, 0
BRIM_RN equ ($ - br_im_rtab) / 4

; AL = a decoder part (71.30): fetched, CF=1 refused; else DX = its code
; segment, AX = its data one. UI task. BX, CX, SI, DI may be clobbered
br_im_rs_pxload:
    push bp
    push ax                         ; the part, at [bp]: op_fetch keeps no
    mov bp, sp                      ; register it is not asked to
    call op_fetch
    jc .no
    mov al, [bp]
    call op_cseg
    or ax, ax
    jz .no
    mov dx, ax
    mov al, [bp]
    call op_seg
    or ax, ax
    jz .no
    pop bp                          ; (the part: AX is the answer)
    pop bp
    clc
    retf
.no:
    mov al, 0                       ; REFUSED WITH PART 0 IN: the two share the
    call op_seg                     ; claims table's last rows (SPEC.md 71.23),
    or ax, ax                       ; so the alt box is said, not left to read
    jz .quiet                       ; as a broken file
    push si
    push cx
    push es
    mov si, br_s_imgdl
    mov cx, 90
    push ds
    pop es
    call OSAPI_TOAST                ; ES:SI, CX ticks
    pop es
    pop cx
    pop si
.quiet:
    pop ax
    pop bp
    stc
    retf
br_im_rs_pxdrop:    call op_drop        ; AL = the part. All kept
                    retf

br_im_rs_imgsub:    call br_imgsub
                    retf
br_im_rs_claimp:    call br_claimp
                    retf
br_im_rs_regrowp:   call br_regrowp
                    retf
br_im_rs_memroom:   call br_memroom
                    retf
br_im_rs_movable:   call br_movable
                    retf
br_im_rs_layout:    call br_layout
                    retf
br_im_rs_advance:   call br_advance
                    retf
br_im_rs_emsframe:  call br_emsframe
                    retf
br_im_rs_argrow:    call br_ar_grow
                    retf
br_im_rs_armap:     call br_ar_map
                    retf
br_im_rs_icenter:   call br_icenter
                    retf
br_im_rs_odback:    call od_back
                    retf
br_im_rs_mkname:    call br_icmkname
                    retf
br_im_rs_icdel:     call br_icdel
                    retf
br_im_rs_icdrop:    call br_icdrop
                    retf

; =============================================================================
; THE DISK TIER (SPEC.md 71.27): SYSTEM\APPDATA\BRCACHE on our own volume
; =============================================================================
br_s_iccache: db 'BRCACHE', 0

; br_icenter - stand in SYSTEM\APPDATA\BRCACHE (os88data.inc's walk, then
; the folder, banked). AL = 1 makes the folder when it is missing. CF=1 not
; there, and the instance back where it stood; CF=0 there, and od_back is
; the way home. UI task. All kept
br_icenter:
    push ax
    push bx
    push dx
    push si
    mov ah, al
    call od_enter
    jc .out
    cmp word [br_iccl], 0
    je .find
    mov bl, [od_drv]
    cmp bl, [br_icdrv]
    jne .find
    mov dx, [br_iccl]
    call od_qm                      ; the bank: a word, or a quiet mount
    jnc .out
.find:
    mov word [br_iccl], 0
    mov si, br_s_iccache
    push ax
    call od_dive                    ; DX = its cluster on CF=0
    pop ax
    jnc .bank
    or ah, ah
    jz .fail
    call OSAPI_FILE_MKDIR           ; in APPDATA: od_dive moved nothing
    jc .fail
    push ax
    call od_dive
    pop ax
    jc .fail
.bank:
    mov [br_iccl], dx
    mov al, [od_drv]
    mov [br_icdrv], al
    clc
    jmp short .out
.fail:
    call od_back
    stc
.out:
    pop si
    pop dx
    pop bx
    pop ax
    ret

; br_icdrop - the page's cache file deleted and the tier reset for the next
; page: br_imgstop's, and a close's once it is accepted. UI task. All kept,
; the flags too
br_icdrop:
    pushf
    push ax
    test byte [br_icstate], ICS_FILE
    jz .reset
    xor al, al
    call br_icenter
    jc .reset
    push bx
    push cx
    push dx
    push si
    push di
    mov si, br_icname
    call OSAPI_FILE_DELETE
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    call od_back
.reset:
    call br_icreset
    pop ax
    popf
    ret

; br_icreset - the tier as a page with no file finds it. All kept
br_icreset:
    push ax
    xor ax, ax
    mov [br_icstate], al
    mov [br_idlen], ax
    mov [br_idlen1], ax
    mov [br_ifend], ax
    mov [br_ifend+2], ax
    pop ax
    ret

; br_icpush - the page goes (br_imgstop): on a fixed or RAM volume its file is
; kept in br_icring, the oldest of BR_ICRING deleted first; else it is
; deleted (SPEC.md 71.28). UI task. All kept
br_icpush:
    test byte [br_icstate], ICS_FILE
    jz br_icdrop
    cmp byte [br_ickeep], 0
    je br_icdrop
    push ax
    push bx
    push cx
    push si
    push di
    push es
    cmp byte [br_icrn], BR_ICRING
    jb .room
    mov ax, [br_icring + 4]         ; the oldest page's file
    call br_icdel
    push ds
    pop es
    mov si, br_icring + 6
    mov di, br_icring
    mov cx, (BR_ICRING - 1) * 3
    cld
    rep movsw
    dec byte [br_icrn]
.room:
    mov al, [br_icrn]
    mov ah, 6
    mul ah
    mov bx, ax
    add bx, br_icring
    mov ax, [br_ichash]
    mov [bx], ax
    mov ax, [br_ichash + 2]
    mov [bx + 2], ax
    mov ax, [br_iccur]
    mov [bx + 4], ax
    inc byte [br_icrn]
    call br_icreset                 ; kept, and no longer this page's
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; br_icclose - an accepted close: the page's file and every kept one.
; UI task. All kept, the flags too
br_icclose:
    pushf
    push ax
    push bx
    call br_icdrop
    mov bx, br_icring
.k:
    cmp byte [br_icrn], 0
    je .out
    mov ax, [bx + 4]
    call br_icdel
    add bx, 6
    dec byte [br_icrn]
    jmp short .k
.out:
    pop bx
    pop ax
    popf
    ret

; br_icdel - AX = a file number: that file deleted from BRCACHE, and
; br_icname the page's own again. UI task. All kept
br_icdel:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call br_icmkname
    xor al, al
    call br_icenter
    jc .name
    mov si, br_icname
    call OSAPI_FILE_DELETE
    call od_back
.name:
    mov ax, [br_iccur]
    call br_icmkname
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; br_icmkname - AX = a file number -> br_icname = Bssssvvv.BRC: our data
; segment and the number, in hex (SPEC.md 71.27). All kept
br_icmkname:
    push ax
    push cx
    push dx
    push di
    push es
    push ds
    pop es
    mov di, br_icname
    push ax
    mov al, 'B'
    stosb
    mov dx, ds
    mov cx, 4
    call .hex
    pop dx
    mov cl, 4
    shl dx, cl
    mov cx, 3
    call .hex
    mov byte [di], '.'
    mov word [di + 1], 'BR'
    mov word [di + 3], 'C'          ; 'C' and the NUL
    pop es
    pop di
    pop dx
    pop cx
    pop ax
    ret
; CX hex digits of DX, from the top, at ES:DI. AX, DX clobbered
.hex:
    push cx
    mov cl, 4
    rol dx, cl
    mov al, dl
    and al, 0Fh
    add al, '0'
    cmp al, '9'
    jbe .d
    add al, 'A' - '9' - 1
.d:
    stosb
    pop cx
    loop .hex
    ret

; br_imgdisk - a slice of a copy in the cache file: AX = the copy's first
; paragraph in the file, SI = the slice's byte in the copy, BP = a row's
; bytes (the slice is eight). From the paint buffer, the store's first
; BR_IDBUF bytes as two BR_IDWIN windows, a miss read from the slice's
; sector into the one the last hit was not in, so two pictures on a row keep
; a window each (71.27). out ES:SI = the slice; CF=1 the cache is off (a
; refused or short read turns it off). UI task. Others kept
br_imgdisk:
    test byte [br_icstate], ICS_OFF
    jz .on
    stc
    ret
.on:
    push ax
    push bx
    push cx
    push dx
    push di
    mov dx, ax
    mov cl, 4
    shl ax, cl
    mov cl, 12
    shr dx, cl
    add ax, si
    adc dx, 0                       ; DX:AX = the slice's byte in the file
    mov di, bp
    shl di, 1
    shl di, 1
    shl di, 1                       ; DI = its bytes
    call .inbuf
    jnc .hit
    push ax
    push dx
    xor al, al
    call br_icenter
    pop dx
    pop ax
    jc .off
    push ax
    push dx
    push si
    push es
    and ax, 0FE00h                  ; from its sector (whole sectors, 18.4.4.2)
    xor bx, bx                      ; into the window the last hit was not in
    mov si, br_idoff
    cmp byte [br_idlast], 0
    jne .win
    mov bx, BR_IDWIN
    mov si, br_idoff1
.win:
    mov [si], ax
    mov [si+2], dx
    mov word [si+4], 0
    mov dx, [br_istseg]
    xor ax, ax
    call br_imgsub
    mov es, dx
    mov cx, BR_IDWIN
    mov ax, [si]
    mov dx, [si+2]
    push si
    mov si, br_icname
    push ax                         ; ES:BX is the movable store: pinned for
    xor ax, ax                      ; the call (SPEC.md 66.3 rule 5)
    call br_pagepin
    pop ax
    call OSAPI_FILE_READ_AT         ; DX:AX = the bytes delivered
    call br_pageunpin               ; AX and the flags kept
    pop si
    jc .read
    mov [si+4], ax                  ; (4 KB at the most: DX = 0)
.read:
    pop es
    pop si
    pop dx
    pop ax
    call od_back                    ; flags kept: the read's answer
    jc .off
    call .inbuf
    jnc .hit
.off:
    or byte [br_icstate], ICS_OFF   ; refused, short or swapped: off for
    mov word [br_idlen], 0          ; the page, and the box is a frame
    mov word [br_idlen1], 0
    stc
    jmp short .out
.hit:
    mov si, cx
    mov dx, [br_istseg]
    xor ax, ax
    call br_imgsub
    mov es, dx                      ; ES:SI = the slice in the buffer
    clc
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret
; DX:AX = a file byte, DI = bytes -> CF=0 CX = its offset in the buffer,
; [br_idlast] the window it is in. AX, DX kept
.inbuf:
    push bx
    mov bx, br_idoff                ; window 0's offset and length, then 1's
.ibw:
    push ax
    push dx
    sub ax, [bx]
    sbb dx, [bx+2]
    jc .ibn
    or dx, dx
    jnz .ibn
    mov cx, ax
    add ax, di
    jc .ibn
    cmp ax, [bx+4]
    ja .ibn
    pop dx
    pop ax
    cmp bx, br_idoff
    mov byte [br_idlast], 0
    je .ibk
    mov byte [br_idlast], 1
    add cx, BR_IDWIN                ; window 1 is the buffer's second half
.ibk:
    pop bx
    clc
    ret
.ibn:
    pop dx
    pop ax
    cmp bx, br_idoff1
    mov bx, br_idoff1
    jne .ibw
    pop bx
    stc
    ret

; =============================================================================
; THE PAINT (SPEC.md 71.26)
; =============================================================================

; br_imgmap - a slice of a copy in EMS into the frame: AX = the copy's first
; paragraph in br_imgar, SI = the slice's byte in the copy. The page holding
; it goes in quarter 0 and the next in quarter 1, which a slice (2,880 bytes
; at most) never passes. out ES:SI = the slice; CF=1 refused. Others kept
br_imgmap:
    push ax
    push cx
    push dx
    mov dx, si
    mov cl, 4
    shr dx, cl
    add ax, dx                      ; AX = the paragraph the slice starts in
    and si, 15
    mov dx, ax
    and ax, 3FFh
    shl ax, cl
    add si, ax                      ; SI = its byte in that page
    mov cl, 10
    shr dx, cl
    mov cx, dx                      ; CX = the page
    push si
    mov si, br_imgar
    xor al, al
    call br_ar_map
    jc .out
    inc cx
    cmp cx, [si+AR_TOTAL]
    jb .q1
    dec cx                          ; (the arena's last page: nothing past it)
.q1:
    mov al, 1
    call br_ar_map
.out:
    pop si
    mov es, [br_emsseg]
    pop dx
    pop cx
    pop ax
    ret

; br_imgblits - blit the ready boxes on the row br_build just built
; in:  CX = the row's pen x, DX = its pen y; br_rimg; [br_ionly] = k + 1 for
;      an images-only pass (only image k), 0 for every ready box
; out: CF=1 some box was drawn, and its record's drawn byte is set. All kept
br_imgblits:
    mov byte [br_idrew], 0
    cmp byte [br_rimgn], 0
    jne .some
    clc
    ret
.some:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov [br_ipx], cx
    mov [br_ipy], dx
    mov word [br_irec], 0
    mov al, [br_rimgn]
    mov [br_icnt], al
.box:
    cmp byte [br_icnt], 0
    jne .one
    jmp .done
.one:
    dec byte [br_icnt]
    mov bx, [br_irec]
    mov al, [br_rimg+bx+4]          ; its image
    cmp al, BR_IMGTMAX
    jae .next
    mov ah, [br_ionly]
    or ah, ah
    jz .any
    dec ah
    cmp al, ah
    jne .next
.any:
    xor ah, ah
    mov si, bx
    mov bx, ax
    call br_imgent                  ; BX = the entry, SI = the record
    mov al, [bx+IT_ST]
    cmp al, IS_READY
    jb .next                        ; waiting
    cmp al, IS_NONE
    je .next                        ; ready in the store, EMS or the file

    mov al, [br_rimg+si+1]
    cmp al, [bx+IT_COLS]            ; made for THIS box
    jne .next
    mov al, [br_rimg+si+3]
    cmp al, [bx+IT_ROWS]
    jne .next
    mov al, [br_rimg+si]            ; AX = its first column
    xor ah, ah
    mov cx, [br_cols]
    sub cx, ax
    jbe .next                       ; wholly past the band
    mov dl, [br_rimg+si+1]
    xor dh, dh
    cmp cx, dx
    jbe .wide
    mov cx, dx                      ; CX = its cells inside the band
.wide:
    push cx
    mov cl, 3
    shl ax, cl
    add ax, [br_ipx]                ; AX = x
    pop cx
    push ax
    push cx
    mov dl, [br_rimg+si+1]
    xor dh, dh
    mov bp, dx                      ; BP = a row's bytes: COLS...
    mov di, dx                      ; ...DI = a plane's step
    cmp byte [br_bpp], 1
    je .stride
    shl bp, 1
    shl bp, 1                       ; ...four planes of them
.stride:
    mov al, [br_rimg+si+2]          ; the slice: its eight rows
    xor ah, ah
    mov cl, 3
    shl ax, cl
    mul bp
    mov si, ax                      ; SI = the slice's first row in the copy
    mov ax, [bx+IT_PARA]
    cmp byte [bx+IT_ST], IS_EMS
    je .ems
    cmp byte [bx+IT_ST], IS_DISK
    je .disk
    mov dx, [br_istseg]
    call br_imgsub
    mov es, dx                      ; ES:SI = it in the store
    jmp short .src
.disk:
    call br_imgdisk                 ; ES:SI = it in the paint buffer
    jmp short .got
.ems:
    call br_imgmap                  ; ES:SI = it in the frame
.got:
    jnc .src
    pop cx
    pop ax
    jmp short .next
.src:
    pop cx
    shl cx, 1
    shl cx, 1
    shl cx, 1                       ; CX = the width in pixels
    pop ax
    mov bx, [br_ipy]
    mov dx, BR_CELL
    cmp byte [br_bpp], 1
    je .mono
    or di, 0x4000                   ; walk an armed clip (SPEC.md 5.4.3.6)
    call OSAPI_GFX_BLITP
    jmp short .drawn
.mono:
    call OSAPI_GFX_BLIT1
.drawn:
    jc .next                        ; refused: the box is drawn as 4.1 drew it
    mov bx, [br_irec]
    mov byte [br_rimg+bx+5], 1
    mov byte [br_idrew], 1
    mov cl, [br_rimg+bx+4]          ; image k drawn: its bit (71.29)
    mov bl, cl
    xor bh, bh
    shr bx, 1
    shr bx, 1
    shr bx, 1
    and cl, 7
    mov al, 1
    shl al, cl
    or [br_iseen+bx], al
.next:
    add word [br_irec], 6
    jmp .box
.done:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    cmp byte [br_idrew], 1          ; CF=1 iff nothing drawn...
    cmc                             ; ...so CF=1 iff something was
    ret

; br_runsegs - the row's text as FONT_RUNs around the boxes br_imgblits drew
; in:  CX/DX = the row's pen; br_lbuf; br_rimg's drawn bytes. All kept
br_runsegs:
    push ax
    push bx
    push si
    xor si, si                      ; SI = the first column not yet drawn
    xor bx, bx
    mov al, [br_rimgn]
    mov [br_icnt], al
.rec:
    cmp byte [br_icnt], 0
    je .tail
    dec byte [br_icnt]
    cmp byte [br_rimg+bx+5], 0
    je .nx
    mov al, [br_rimg+bx]
    xor ah, ah
    call br_runseg                  ; up to the box
    mov si, ax
    mov al, [br_rimg+bx+1]
    xor ah, ah
    add si, ax                      ; ...and on past it
.nx:
    add bx, 6
    jmp short .rec
.tail:
    mov ax, [br_cols]
    call br_runseg
    pop si
    pop bx
    pop ax
    ret

; br_runseg - one FONT_RUN of br_lbuf's columns SI up to AX, at the pen CX/DX
; All kept
br_runseg:
    push ax
    push bx
    push cx
    push si
    cmp ax, [br_cols]
    jbe .end
    mov ax, [br_cols]
.end:
    cmp si, ax
    jae .out
    mov bx, ax
    add bx, br_lbuf
    mov al, [bx]
    push ax
    mov byte [bx], 0                ; the run stops at the box
    mov ax, si
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add cx, ax
    add si, br_lbuf
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    pop ax
    mov [bx], al
.out:
    pop si
    pop cx
    pop bx
    pop ax
    ret
