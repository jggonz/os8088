; =============================================================================
; os8088 - apps/pixel/pxfold.asm
;
; PiXEL's FOLDER PART (SPEC.md 106.21, 106.20): part 5 of PIXEL.O88, LINKED
; against the package (build/pxlink.inc) - the thumbnail cache,
; SYSTEM/APPDATA/PIXEL.THC, and the making of a thumbnail from a master: the
; cold half of the filmstrip's thumbnails, fetched by the UI task when the
; strip has a use for it and dropped again, so its bytes are not the
; resident package's. The resident keeps what a paint needs - the store, a
; card's key, the drawing - and the engine that decides when.
;
; One vector does the work, DECODE, with a VERB in CL (pxthc.inc's PF_*):
;
;   PF_VISIT  the cache's header read for the strip's folder when it was read
;             for another (or not yet), then every card on show whose
;             thumbnail the store lacks and the cache holds read into a slot -
;             ONE walk to SYSTEM/APPDATA and back. AX = the cards read
;   PF_WRITE  the header read again (another PiXEL may have written), every
;             NEW slot written to its entry (its own, a new one while the disk
;             keeps 32 KB free, else the least recently used), the entries
;             read since the last write stamped, the header last.
;             CF = 1 AX = 1 the volume refused (the resident says so once)
;   PF_MAKE   the thumbnail of name [px_tname] from [px_cur]'s master through
;             [px_pal] into the slot already holding it or a free one (none
;             free: CF = 1); the cube's 1bpp thresholds made the first time
;
; The resident pins the store round every call: these transfers land in it.
; A part may call the API (a cell is a far call made with the package's DS,
; SPEC.md 20.3, and the file cells resolve in the instance the dispatched
; callback is stamped with, 19.2.1): DS is the package throughout, the part's
; own strings and tables are reached through CS - and the three names the
; file cells are handed are copied into the package's [px_line] first, since
; a cell reads its name through DS. It never speaks: the resident words a
; refusal.
; =============================================================================

%include "pxpart.inc"
%include "os88api.inc"
%include "pxrec.inc"
%include "pxthc.inc"
%include "pxlink.inc"

    cpu 8086
    bits 16
    org 0

    PXPART_HEAD pf_init, pf_decode, pf_none, pf_none, pf_none

PF_CROOM    equ 4096 + 32768        ; the free bytes a new entry needs

; PXV_INIT - out AX = PXP_PROBE
pf_init:
    mov ax, PXP_PROBE
    clc
    retf

; PXV_INFO, PXV_HEAD, PXV_PLANS - not this part's
pf_none:
    mov ax, PXE_NOTSUP
    stc
    retf

; PXV_DECODE - CL = a verb: DS = the package. out CF/AX as the verb says.
; Preserves all but AX
pf_decode:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push bp
    cmp word [8], PXL_IMAGE         ; THE STAMP (SPEC.md 106.20): another
    jne .link                       ; build's package is not read
    cmp word [10], PXL_BSS
    jne .link
    cld
    cmp cl, PF_VISIT
    je .v
    cmp cl, PF_WRITE
    je .w
    cmp cl, PF_MAKE
    jne .bad
    call pf_make
    jmp short .out
.v:
    call pf_visit
    jmp short .out
.w:
    call pf_write
    jmp short .out
.bad:
    mov ax, PXE_NOTSUP
    stc
    jmp short .out
.link:
    mov ax, PXD_LINK
    stc
.out:
    pop bp
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    retf

; =============================================================================
; THE STORE AND THE KEYS (the resident's own, again: a part cannot call it)
; =============================================================================

; pf_rec - AX = a name: SI = its record. Preserves all but SI
pf_rec:
    push ax
    push dx
    mov si, PX_NREC
    mul si
    add ax, px_names
    mov si, ax
    pop dx
    pop ax
    ret

; pf_slotdi - BX = a slot: DI = its first byte. Preserves all but DI
pf_slotdi:
    push ax
    push dx
    mov ax, PXT_SLSZ
    mul bx
    add ax, PXT_SL0
    mov di, ax
    pop dx
    pop ax
    ret

; pf_tkmake - AX = a name: [px_tkey] := its key. Preserves all
pf_tkmake:
    push ax
    push cx
    push si
    push di
    push es
    push ds
    pop es
    push ax
    mov di, px_tkey
    xor ax, ax
    mov cx, PX_TKSZ / 2
    rep stosw
    pop ax
    call pf_rec
    push si
    mov di, px_tkey + TK_NAME
    mov cx, 12
.n:
    mov al, [si]
    or al, al
    jz .nd
    mov [di], al
    inc si
    inc di
    loop .n
.nd:
    pop si
    mov ax, [si + NR_CLUS]
    mov [px_tkey + TK_CLUS], ax
    mov ax, [si + NR_SIZE]
    mov [px_tkey + TK_SIZE], ax
    mov ax, [si + NR_SIZE + 2]
    mov [px_tkey + TK_SIZE + 2], ax
    mov ax, [px_nfdir]
    mov [px_tkey + TK_DIR], ax
    mov al, [px_nfvol]
    mov [px_tkey + TK_VOL], al
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; pf_tkcmp - ES:DI = a key: ZF = 1 when it names [px_tkey]'s picture.
; Preserves all but the flags
pf_tkcmp:
    push cx
    push si
    push di
    mov si, px_tkey
    mov cx, TK_CMP
    repe cmpsb
    pop di
    pop si
    pop cx
    ret

; pf_tsfind - [px_tkey]: CF = 0 BX = the slot holding it. Preserves all but BX
pf_tsfind:
    push di
    push es
    mov es, [px_tseg]
    xor bx, bx
.s:
    call pf_slotdi
    add di, PX_TDATA
    test byte [es:di + TK_FLAGS], TKF_VALID
    jz .n
    call pf_tkcmp
    je .yes
.n:
    inc bx
    cmp bx, PX_TSLOTS
    jb .s
    pop es
    pop di
    stc
    ret
.yes:
    pop es
    pop di
    clc
    ret

; pf_tsvis - BX = a slot: CF = 0 when a card on show is its picture. Makes
; keys. Preserves all
pf_tsvis:
    push ax
    push cx
    push dx
    mov dx, bx
    mov ax, [px_fcs]
    mov cx, [px_fcn]
    jcxz .no
.c:
    cmp ax, [px_nnames]
    jae .no
    call pf_tkmake
    call pf_tsfind
    jc .n
    cmp bx, dx
    je .yes
.n:
    inc ax
    loop .c
.no:
    mov bx, dx
    pop dx
    pop cx
    pop ax
    stc
    ret
.yes:
    mov bx, dx
    pop dx
    pop cx
    pop ax
    clc
    ret

; pf_tsalloc - BX = a slot a new thumbnail may take: an empty one, else one
; no card on show is - one already in the cache before one that is not.
; CF = 1 none. Makes keys. Preserves all but BX
pf_tsalloc:
    push ax
    push di
    push es
    mov es, [px_tseg]
    xor bx, bx
.e:
    call pf_slotdi
    test byte [es:di + PX_TDATA + TK_FLAGS], TKF_VALID
    jz .yes
    inc bx
    cmp bx, PX_TSLOTS
    jb .e
    mov al, TKF_NEW                 ; pass 1 passes the new ones by
.pass:
    xor bx, bx
.v:
    call pf_slotdi
    test [es:di + PX_TDATA + TK_FLAGS], al
    jnz .vn
    call pf_tsvis
    jc .yes
.vn:
    inc bx
    cmp bx, PX_TSLOTS
    jb .v
    or al, al
    jz .none
    xor al, al
    jmp short .pass
.none:
    pop es
    pop di
    pop ax
    stc
    ret
.yes:
    pop es
    pop di
    pop ax
    clc
    ret

; =============================================================================
; PF_MAKE (SPEC.md 106.21)
; =============================================================================
pf_make:
    mov ax, [px_tname]
    call pf_tkmake
    call pf_tsfind                  ; (a re-made one replaces itself)
    jnc .have
    call pf_tsalloc
    jnc .have
    mov ax, 1                       ; no slot: the card keeps its name
    stc
    ret
.have:
    mov [px_tslot], bx
    mov ax, [px_tname]
    call pf_tkmake                  ; (pf_tsalloc's walk made other keys)
    mov ax, [px_cur + PXR_MW]
    mov bx, [px_cur + PXR_MH]
    call pf_tfit
    mov al, [px_ttw]
    mov [px_tkey + TK_TW], al
    mov al, [px_tth]
    mov [px_tkey + TK_TH], al
    mov byte [px_tkey + TK_FLAGS], TKF_VALID | TKF_NEW
    call pf_cfind                   ; the cache has it already: nothing new
    jc .new
    mov byte [px_tkey + TK_FLAGS], TKF_VALID
.new:
    call pf_tmap                    ; the palette onto the cube
    mov ax, [px_cur + PXR_MW]       ; the column step: mw / tw, 16.16
    xor dx, dx
    mov cl, [px_ttw]
    xor ch, ch
    div cx
    mov [cs:pf_step], ax
    xor ax, ax
    div cx
    mov bp, ax
    mov bx, [px_tslot]
    call pf_slotdi
    mov es, [px_tseg]
    mov word [cs:pf_y], 0
.row:
    mov ax, [cs:pf_y]
    cmp al, [px_tth]
    jae .done
    mul word [px_cur + PXR_MH]      ; the master row: y mh / th
    mov cl, [px_tth]
    xor ch, ch
    div cx
    mul word [px_cur + PXR_MW]      ; DX:AX = its offset in the master
    mov si, ax
    and si, 15
    mov cl, 4
    shr ax, cl
    mov cl, 12
    shl dx, cl
    or ax, dx
    add ax, [px_cur + PXR_MSEG]
    mov cl, [px_ttw]
    xor ch, ch
    mov bx, PXT_MAP
    xor dx, dx                      ; DX = the fraction
    push ds
    mov ds, ax
.x:
    mov al, [si]
    es xlatb                        ; through the map, in the store
    stosb
    add dx, bp
    adc si, [cs:pf_step]
    loop .x
    pop ds
    inc word [cs:pf_y]
    jmp short .row
.done:
    mov bx, [px_tslot]              ; the key after the bytes
    call pf_slotdi
    add di, PX_TDATA
    mov si, px_tkey
    mov cx, PX_TKSZ
    rep movsb
    test byte [px_tkey + TK_FLAGS], TKF_NEW
    jz .old
    mov byte [px_cnew], 1           ; (the cache has something to write)
.old:
    inc word [px_thmade]
    call pf_t1
    xor ax, ax
    clc
    ret

pf_step:    dw 0                    ; the part's own scratch, through CS
pf_y:       dw 0

; pf_tfit - AX = a width, BX = a height, both > 0: [px_ttw] x [px_tth], the
; largest PX_TW x PX_TH box of that shape. Preserves all
pf_tfit:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, ax                      ; SI = w
    mov cx, bx                      ; CX = h
    mov dx, ax                      ; wide, when 3w >= 4h (w/h >= 72/54)
    shl ax, 1
    add ax, dx
    mov dx, cx
    shl dx, 1
    shl dx, 1
    cmp ax, dx
    jb .tall
    mov ax, si                      ; tw = min(72, w), th = h tw / w
    cmp ax, PX_TW
    jbe .w1
    mov ax, PX_TW
.w1:
    mov [px_ttw], al
    mul cx
    mov bx, si
    shr bx, 1
    add ax, bx
    adc dx, 0
    div si
    mov bx, PX_TH
    jmp short .clamp
.tall:
    mov ax, cx                      ; th = min(54, h), tw = w th / h
    cmp ax, PX_TH
    jbe .t1
    mov ax, PX_TH
.t1:
    mov [px_tth], al
    mul si
    mov bx, cx
    shr bx, 1
    add ax, bx
    adc dx, 0
    div cx
    mov bx, PX_TW
.clamp:
    cmp ax, bx                      ; 1 .. the box's side
    jbe .c1
    mov ax, bx
.c1:
    or ax, ax
    jnz .c2
    inc ax
.c2:
    cmp bx, PX_TH
    jne .cw
    mov [px_tth], al
    jmp short .out
.cw:
    mov [px_ttw], al
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; pf_tmap - the store's map: each entry of [px_pal] to the nearest cube
; colour (106.8's cube: (5v + 127) div 255 a level, the seven greens'
; (6v + 127) div 255), an exact grey to the cube's nearest grey - so a grey
; picture's thumbnail has no tint. Preserves all
pf_tmap:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov es, [px_tseg]
    mov di, PXT_MAP
    mov si, px_pal
    mov cx, 256
.e:
    mov al, [si]
    cmp al, [si + 1]
    jne .col
    cmp al, [si + 2]
    jne .col
    call pf_q6                      ; a grey: k of five
    mov bx, pf_grey
    cs xlatb
    jmp short .st
.col:
    call pf_q6                      ; (q6 r x 7 + q7 g) x 6 + q6 b
    mov dl, 7
    mul dl
    mov dl, al
    mov al, [si + 1]
    call pf_q7
    add dl, al
    mov al, dl
    mov dl, 6
    mul dl
    mov dl, al
    mov al, [si + 2]
    call pf_q6
    add al, dl
.st:
    stosb
    add si, 3
    loop .e
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; pf_q6 / pf_q7 - AL = a level 0..255: AL = its nearest of six (seven).
; Preserves all but AX
pf_q6:
    push bx
    mov bl, 5
    jmp short pf_qn
pf_q7:
    push bx
    mov bl, 6
pf_qn:
    mul bl
    add ax, 127
    mov bl, 255
    div bl
    pop bx
    ret

; the cube's black, its four neutral greys (SPEC.md 106.8) and its white
pf_grey:    db 0, 49, 98, 153, 202, 251
pf_r7:      db 0, 43, 85, 128, 170, 213, 255

; pf_t1 - the cube's 1bpp thresholds into the store the first time: each
; entry's colour as px_setpal makes it, then px_t1for's (64 L + 127) div
; 255, L = (Y + (Y^2 + 127) div 255) >> 1 (pxview.inc). Preserves all
pf_t1:
    cmp byte [px_tt1ok], 0
    je .go
    ret
.go:
    push ax
    push bx
    push cx
    push dx
    push es
    mov es, [px_tseg]
    xor bx, bx
.e:
    mov al, bl
    call pf_cube                    ; CL, CH, DL = its R, G, B
    mov al, cl                      ; Y = (77 R + 150 G + 29 B) >> 8
    mov ah, 77
    mul ah
    push ax
    mov al, ch
    mov ah, 150
    mul ah
    pop cx
    add cx, ax
    mov al, dl
    mov ah, 29
    mul ah
    add ax, cx
    mov al, ah
    xor ah, ah                      ; AX = Y
    mov cx, ax
    mul ax
    add ax, 127
    adc dx, 0
    push bx
    mov bx, 255
    div bx                          ; (Y^2 + 127) div 255
    add ax, cx
    shr ax, 1                       ; L
    mov dx, 64
    mul dx
    add ax, 127
    adc dx, 0
    div bx
    pop bx
    mov [es:PXT_T1 + bx], al
    inc bx
    cmp bx, 256
    jb .e
    mov byte [px_tt1ok], 1
    pop es
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; pf_cube - AL = a cube index: CL, CH, DL = its colour, as px_setpal makes it
; (the four NEUTRAL codes and 252..255 exact greys). Preserves all else
pf_cube:
    push ax
    push bx
    cmp al, 252
    jae .grey
    mov ah, 1
    cmp al, 49
    je .n
    inc ah
    cmp al, 98
    je .n
    inc ah
    cmp al, 153
    je .n
    inc ah
    cmp al, 202
    je .n
    xor ah, ah                      ; (r x 7 + g) x 6 + b
    mov bl, 42
    div bl                          ; AL = r, AH = g x 6 + b
    mov bh, ah
    mov bl, 51
    mul bl
    mov cl, al
    mov al, bh
    xor ah, ah
    mov bl, 6
    div bl                          ; AL = g, AH = b
    mov bh, ah
    mov bl, al
    push bx
    xor bh, bh
    mov ch, [cs:pf_r7 + bx]
    pop bx
    mov al, bh
    mov bl, 51
    mul bl
    mov dl, al
    jmp short .out
.n:
    mov al, ah
    jmp short .g
.grey:
    sub al, 251
.g:
    mov bl, 51
    mul bl
    mov cl, al
    mov ch, al
    mov dl, al
.out:
    pop bx
    pop ax
    ret

; =============================================================================
; THE CACHE (SPEC.md 106.21, 19.9)
; =============================================================================

; pf_name - SI = a name in this part: copied to the package's [px_line], where
; a file cell can read it through DS. out SI = px_line. Preserves all else
pf_name:
    push ax
    push di
    mov di, px_line
.c:
    mov al, [cs:si]
    mov [di], al
    inc si
    inc di
    or al, al
    jnz .c
    mov si, px_line
    pop di
    pop ax
    ret

; pf_cgo - stand in SYSTEM/APPDATA on PiXEL's own volume (19.9: bank, then
; OSAPI_FILE_GOTO_QM a folder at a time); its cluster must divide 4096 (an
; entry is read and written whole). CF = 1 there is no cache, the instance
; where it was. Preserves all
pf_cgo:
    push ax
    push bx
    push cx
    push dx
    push si
    call OSAPI_FILE_HERE
    mov [px_cwas], dx
    mov [px_cwvol], bl
    xor dx, dx
    mov bl, [px_homevol]
    call OSAPI_FILE_GOTO_QM         ; the root of our own volume
    jc .back
    mov si, pf_s_sys
    call pf_cdive
    jc .back
    mov si, pf_s_app
    call pf_cdive
    jc .back
    call OSAPI_VOL_STAT             ; AX = sectors a cluster, CX = their bytes
    jc .back
    mul cx
    or dx, dx
    jnz .back
    cmp ax, 4096
    ja .back
    clc
    jmp short .out
.back:
    call pf_cback
    stc
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; pf_cback - the instance back where pf_cgo found it. Preserves all, the
; flags too
pf_cback:
    pushf
    push ax
    push bx
    push dx
    mov dx, [px_cwas]
    mov bl, [px_cwvol]
    call OSAPI_FILE_GOTO_QM
    pop dx
    pop bx
    pop ax
    popf
    ret

; pf_cdive - SI = a folder's name (this part's): the instance stands in it.
; CF = 1 there is none. Preserves all
pf_cdive:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov [cs:pf_dv], si              ; (banked: a DVK_FILE volume's file cells
    xor cx, cx                      ; may not keep SI, SPEC.md 92.14)
.f:
    push ds
    pop es
    mov di, px_find
    call OSAPI_FILE_FIND
    jc .out
    cmp word [px_find + 14], OSAPI_FT_DIR
    jne .f
    mov si, [cs:pf_dv]
    mov di, px_find
.cmp:
    mov al, [cs:si]
    cmp al, [di]
    jne .f
    inc si
    inc di
    or al, al
    jnz .cmp
    mov dx, [px_find + 16]
    mov bl, [px_homevol]
    call OSAPI_FILE_GOTO_QM
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

pf_dv:      dw 0

; pf_chdr - the header read into the store (standing in APPDATA); one that is
; not a header of ours is an empty one. CF = 1 the file is not there.
; Preserves all
pf_chdr:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov es, [px_tseg]
    mov bx, PXT_BUF                 ; INTO THE BUFFER, and only the header's
    mov cx, 4096                    ; own bytes copied down: the 4 KB past it
                                    ; are the store's plans, thresholds and
                                    ; map (pxthc.inc), and a file's would be
                                    ; believed (review-w5 F1)
    xor ax, ax
    xor dx, dx
    mov si, pf_s_thc
    call pf_name
    call OSAPI_FILE_READ_AT         ; DX:AX = the bytes delivered
    jc .none
    mov es, [px_tseg]
    or dx, dx
    jnz .chk
    cmp ax, 4096
    jb .bad
.chk:
    cmp word [es:PXT_BUF], 'PX'
    jne .bad
    cmp word [es:PXT_BUF + 2], 'TC'
    jne .bad
    cmp byte [es:PXT_BUF + CH_VER], 1
    jne .bad
    cmp byte [es:PXT_BUF + CH_COUNT], PX_CMAX
    ja .bad
    push ds
    push es
    pop ds
    mov si, PXT_BUF
    mov di, PXT_HDR
    mov cx, (CH_KEYS + PX_CMAX * PX_TKSZ) / 2
    cld
    rep movsw
    pop ds
    clc
    jmp short .out
.bad:                               ; short, empty or not ours: as if there
.none:                              ; were none, so the writer makes the
    call pf_chempty                 ; file anew whole rather than write at
    stc                             ; an offset it may not have (review-w5
.out:                               ; F3)
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; pf_hbuf - the store's header into PXT_BUF as the file's 4 KB: its own
; bytes, then zeroes (never the store's plans past it, review-w5 F1).
; Preserves all
pf_hbuf:
    push ax
    push cx
    push si
    push di
    push ds
    push es
    mov es, [px_tseg]
    push es
    pop ds
    mov si, PXT_HDR
    mov di, PXT_BUF
    mov cx, (CH_KEYS + PX_CMAX * PX_TKSZ) / 2
    cld
    rep movsw
    mov cx, (4096 - CH_KEYS - PX_CMAX * PX_TKSZ) / 2
    xor ax, ax
    rep stosw
    pop es
    pop ds
    pop di
    pop si
    pop cx
    pop ax
    ret

; pf_chempty - the store's header an empty one. Preserves all
pf_chempty:
    push es
    mov es, [px_tseg]
    mov word [es:PXT_HDR], 'PX'
    mov word [es:PXT_HDR + 2], 'TC'
    mov word [es:PXT_HDR + CH_VER], 1   ; version 1, no entries
    mov word [es:PXT_HDR + CH_CLOCK], 0
    pop es
    ret

; pf_cfind - CF = 0 BX = the entry whose key names [px_tkey]'s picture.
; Preserves all but BX
pf_cfind:
    cmp byte [px_cstat], 1
    jne .no
    push di
    push es
    mov es, [px_tseg]
    xor bx, bx
    mov di, PXT_HDR + CH_KEYS
.e:
    cmp bl, [es:PXT_HDR + CH_COUNT]
    jae .no1
    test byte [es:di + TK_FLAGS], TKF_VALID
    jz .n
    call pf_tkcmp
    je .yes
.n:
    add di, PX_TKSZ
    inc bx
    jmp short .e
.no1:
    pop es
    pop di
.no:
    stc
    ret
.yes:
    pop es
    pop di
    clc
    ret

; pf_off - BX = an entry: DX:AX = its offset in the file, (entry + 1) x 4096.
; Preserves all else
pf_off:
    push cx
    mov ax, bx
    inc ax
    mov dx, ax
    mov cl, 12
    shl ax, cl
    mov cl, 4
    shr dx, cl
    pop cx
    ret

; =============================================================================
; PF_VISIT
; =============================================================================
pf_visit:
    mov byte [cs:pf_in], 0
    mov word [cs:pf_got], 0
    cmp byte [px_cstat], 1          ; the header, when it was read for another
    jne .load                       ; folder or not at all
    mov ax, [px_cdir]
    cmp ax, [px_nfdir]
    jne .load
    mov al, [px_cvol]
    cmp al, [px_nfvol]
    je .fill
.load:
    call pf_cgo
    jc .none
    mov byte [cs:pf_in], 1
    call pf_chdr
    call pf_chitclr                 ; (hits against another header)
    mov byte [px_cstat], 1
    mov ax, [px_nfdir]
    mov [px_cdir], ax
    mov al, [px_nfvol]
    mov [px_cvol], al
.fill:
    mov ax, [px_fcs]                ; every card on show...
    mov cx, [px_fcn]
    jcxz .done
.c:
    cmp ax, [px_nnames]
    jae .done
    call pf_rec
    test byte [si + NR_FLAGS], NRF_NOTH
    jnz .n
    call pf_tkmake
    call pf_tsfind                  ; ...not held already...
    jnc .n
    call pf_cfind                   ; ...that the cache holds...
    jc .n
    mov dx, bx
    call pf_tsalloc                 ; ...and a slot can take
    jc .n
    call pf_tkmake                  ; (pf_tsalloc's walk made other keys)
    cmp byte [cs:pf_in], 0
    jne .in
    call pf_cgo
    jc .gone
    mov byte [cs:pf_in], 1
.in:
    call pf_cread
    jc .n
    inc word [cs:pf_got]
    inc word [px_thread]
.n:
    inc ax
    loop .c
    jmp short .done
.gone:
    mov byte [px_cstat], 2
.done:
    cmp byte [cs:pf_in], 0
    je .t
    call pf_cback
.t:
    cmp word [cs:pf_got], 0
    je .r
    call pf_t1
.r:
    mov ax, [cs:pf_got]
    clc
    ret
.none:
    mov byte [px_cstat], 2          ; no SYSTEM/APPDATA: no cache, nothing said
    xor ax, ax
    clc
    ret

pf_in:      db 0
pf_got:     dw 0

; pf_cread - DX = an entry, BX = a slot, [px_tkey] the picture's key: the
; entry read and, its own key the header's, into the slot (not NEW); the hit
; noted. CF = 1 not read. Standing in APPDATA. Preserves all
pf_cread:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov [px_tslot], bx
    mov [px_centry], dx
    mov bx, dx
    call pf_off
    mov es, [px_tseg]
    mov bx, PXT_BUF
    mov cx, 4096
    mov si, pf_s_thc
    call pf_name
    call OSAPI_FILE_READ_AT
    jc .no
    or dx, dx
    jnz .got
    cmp ax, PXT_SLSZ
    jb .no
.got:
    mov es, [px_tseg]
    mov di, PXT_BUF + PX_TDATA
    call pf_tkcmp                   ; its own key the header's
    jne .no
    mov al, [es:di + TK_TW]
    dec al
    cmp al, PX_TW
    jae .no
    mov al, [es:di + TK_TH]
    dec al
    cmp al, PX_TH
    jae .no
    mov byte [es:di + TK_FLAGS], TKF_VALID
    mov bx, [px_tslot]              ; into the slot
    call pf_slotdi
    push ds
    mov ax, es
    mov ds, ax
    mov si, PXT_BUF
    mov cx, PXT_SLSZ / 2
    rep movsw
    pop ds
    mov bx, [px_centry]             ; a hit: stamped at the next write
    mov si, bx
    mov cl, 3
    shr si, cl
    and bx, 7
    mov al, [cs:pf_bit + bx]
    or [px_chit + si], al
    mov byte [px_cnew], 1
    clc
    jmp short .out
.no:
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

pf_bit:     db 1, 2, 4, 8, 16, 32, 64, 128

; pf_chitclr - no hits. Preserves all
pf_chitclr:
    mov word [px_chit], 0
    mov word [px_chit + 2], 0
    mov word [px_chit + 4], 0
    mov word [px_chit + 6], 0
    ret

; =============================================================================
; PF_WRITE
; =============================================================================
pf_write:
    call pf_cgo
    jnc .in
    mov byte [px_cstat], 2          ; (the folder went: nothing to say)
    jmp .drop
.in:
    call pf_chdr                    ; read again: CF = the file is not there
    mov byte [cs:pf_miss], 0
    jnc .ex
    mov byte [cs:pf_miss], 1
.ex:
    mov byte [px_cstat], 1
    mov es, [px_tseg]
    inc word [es:PXT_HDR + CH_CLOCK]    ; this write's stamp
    call pf_chits
    cmp byte [cs:pf_miss], 0        ; a new file: its header first
    je .slots
    call pf_hbuf
    mov si, pf_s_thc
    call pf_name
    mov bx, PXT_BUF
    mov cx, 4096
    xor dx, dx
    call OSAPI_FILE_WRITE
    jc .fail
.slots:
    xor bx, bx
.s:
    call pf_slotdi
    mov es, [px_tseg]
    test byte [es:di + PX_TDATA + TK_FLAGS], TKF_NEW
    jz .sn
    call pf_cput
    jc .fail
.sn:
    inc bx
    cmp bx, PX_TSLOTS
    jb .s
    call pf_hbuf                    ; the header last
    mov es, [px_tseg]
    mov bx, PXT_BUF
    mov cx, 4096
    xor ax, ax
    xor dx, dx
    mov si, pf_s_thc
    call pf_name
    call OSAPI_FILE_WRITE_AT
    jc .fail
    call pf_cback
    call pf_chitclr
    mov byte [px_cnew], 0
    inc word [px_thwrote]
    xor ax, ax
    clc
    ret
.fail:
    call pf_cback
    call pf_drop
    mov ax, 1                       ; refused: the resident says so, once
    stc
    ret
.drop:
    call pf_drop
    xor ax, ax
    clc
    ret

pf_miss:    db 0

; pf_drop - nothing more to write: NEW cleared, the hits forgotten.
; Preserves all
pf_drop:
    push bx
    push di
    push es
    mov es, [px_tseg]
    xor bx, bx
.s:
    call pf_slotdi
    and byte [es:di + PX_TDATA + TK_FLAGS], ~TKF_NEW
    inc bx
    cmp bx, PX_TSLOTS
    jb .s
    call pf_chitclr
    mov byte [px_cnew], 0
    pop es
    pop di
    pop bx
    ret

; pf_cput - BX = a NEW slot (standing in APPDATA, the header read): written
; to its entry, the header's key set and stamped, the slot no longer NEW.
; CF = 1 refused. Preserves all
pf_cput:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov [px_tslot], bx
    call pf_slotdi                  ; [px_tkey] := its key
    mov ax, [px_tseg]
    push ds
    push ds
    pop es
    mov ds, ax
    lea si, [di + PX_TDATA]
    mov di, px_tkey
    mov cx, PX_TKSZ / 2
    rep movsw
    pop ds
    call pf_cfind                   ; its own entry...
    jnc .have
    mov es, [px_tseg]               ; ...a new one while the disk has room...
    mov bl, [es:PXT_HDR + CH_COUNT]
    xor bh, bh
    cmp bx, PX_CMAX
    jae .lru
    push bx
    call OSAPI_FILE_DFREE           ; DX:AX = free bytes
    pop bx
    jc .lru
    or dx, dx
    jnz .grow
    cmp ax, PF_CROOM
    jb .lru
.grow:
    mov es, [px_tseg]
    inc byte [es:PXT_HDR + CH_COUNT]
    jmp short .have
.lru:
    call pf_clru                    ; ...else the least recently used
    jc .out
.have:
    mov [px_centry], bx
    mov bx, [px_tslot]              ; the slot staged whole in the buffer
    call pf_slotdi
    mov ax, [px_tseg]
    push ds
    mov ds, ax
    mov es, ax
    mov si, di
    mov di, PXT_BUF
    mov cx, PXT_SLSZ / 2
    rep movsw
    pop ds
    mov byte [es:PXT_BUF + PX_TDATA + TK_FLAGS], TKF_VALID
    mov ax, [es:PXT_HDR + CH_CLOCK]
    mov [es:PXT_BUF + PX_TDATA + TK_STAMP], ax
    mov bx, [px_centry]             ; written at (entry + 1) x 4096
    call pf_off
    mov bx, PXT_BUF
    mov cx, 4096
    mov si, pf_s_thc
    call pf_name
    call OSAPI_FILE_WRITE_AT
    jc .out
    mov ax, [px_centry]             ; the header's key: the staged one
    mov cl, 5
    shl ax, cl
    add ax, PXT_HDR + CH_KEYS
    mov di, ax
    mov ax, [px_tseg]
    push ds
    mov ds, ax
    mov es, ax
    mov si, PXT_BUF + PX_TDATA
    mov cx, PX_TKSZ / 2
    rep movsw
    pop ds
    mov bx, [px_tslot]              ; and the slot is in the cache now
    call pf_slotdi
    mov byte [es:di + PX_TDATA + TK_FLAGS], TKF_VALID
    clc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; pf_clru - BX = the entry stamped longest ago, none stamped by this write.
; CF = 1 none. Preserves all but BX
pf_clru:
    push ax
    push cx
    push dx
    push di
    push es
    mov es, [px_tseg]
    mov dx, [es:PXT_HDR + CH_CLOCK]
    mov bx, 0xFFFF
    mov ax, 0xFFFF
    xor cx, cx
    mov di, PXT_HDR + CH_KEYS
.e:
    cmp cl, [es:PXT_HDR + CH_COUNT]
    jae .d
    cmp [es:di + TK_STAMP], dx
    je .n
    cmp [es:di + TK_STAMP], ax
    jae .n
    mov ax, [es:di + TK_STAMP]
    mov bx, cx
.n:
    add di, PX_TKSZ
    inc cx
    jmp short .e
.d:
    cmp bx, 0xFFFF
    jne .ok
    stc
    jmp short .out
.ok:
    clc
.out:
    pop es
    pop di
    pop dx
    pop cx
    pop ax
    ret

; pf_chits - the entries read since the last write stamped with this one's
; clock. Preserves all
pf_chits:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov es, [px_tseg]
    mov dx, [es:PXT_HDR + CH_CLOCK]
    xor bx, bx
    mov di, PXT_HDR + CH_KEYS
.e:
    cmp bl, [es:PXT_HDR + CH_COUNT]
    jae .out
    mov si, bx
    mov cl, 3
    shr si, cl
    mov al, [px_chit + si]
    mov cl, bl
    and cl, 7
    shr al, cl
    test al, 1
    jz .n
    mov [es:di + TK_STAMP], dx
.n:
    add di, PX_TKSZ
    inc bx
    jmp short .e
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

pf_s_sys:   db 'SYSTEM', 0
pf_s_app:   db 'APPDATA', 0
pf_s_thc:   db 'PIXEL.THC', 0
