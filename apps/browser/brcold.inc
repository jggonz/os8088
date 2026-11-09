; =============================================================================
; THE RARELY USED UI, IN PART 0 (SPEC.md 71.24, roadmap S3)
; =============================================================================

; --- frame and place the context panel inside the current band --------------
br_cx_ctxshow:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, [br_win]
    call far [br_dl_r_measure]
    mov bx, [br_win]
    call OSAPI_WM_CLIP_SET          ; the popup must not paint beyond this window
    jc .clipfail
    cmp byte [br_ctxopen], 0
    je .choose
    mov byte [br_ctxopen], 0
    call br_cx_ctxerase                 ; GFX_FRAME is XOR: erase its whole old box
    call far [br_dl_r_paint_band]       ; replace an existing popup before drawing
    call far [br_dl_r_sbar]             ; the popup must not leave marks on the bar
.choose:
    mov byte [br_ctxkind], 0
    mov byte [br_ctxh], BR_CTXH
    mov word [br_ctxlink], 0FFFFh
    ; Resolve the pointer first. A retained text selection must not mask the
    ; link-specific actions when the user right-clicks directly on a link.
.hit:
    mov cx, [br_ctxpx]
    mov dx, [br_ctxpy]
    call far [br_dl_r_hitofs]
    jc .selection
    call far [br_dl_r_linkat]
    jc .selection
    mov [br_ctxlink], ax
    mov byte [br_ctxkind], 1
    mov byte [br_ctxh], BR_CTXH_LINK
    jmp .measure
.selection:
    cmp byte [br_selkind], 0
    je .measure
    mov byte [br_ctxkind], 2
    mov byte [br_ctxh], BR_CTXH_COPY
.measure:
    ; Size each popup from its actual longest label. The old per-kind fixed
    ; widths left a broad empty strip beside short menu text.
    cmp byte [br_ctxkind], 2
    je .measurecopy
    cmp byte [br_ctxkind], 1
    je .measurelink
    mov si, br_s_ctxsave
    call OSAPI_FONT_WIDTH
    mov di, ax
    cmp byte [br_viewsrc], 0
    jne .measureclose
    mov si, br_s_ctxview
    call OSAPI_FONT_WIDTH
    jmp .measuremax
.measureclose:
    mov si, br_s_ctxclose
    call OSAPI_FONT_WIDTH
.measuremax:
    cmp ax, di
    jae .measurepad
    mov ax, di
    jmp .measurepad
.measurelink:
    mov si, br_s_ctxopen
    call OSAPI_FONT_WIDTH
    mov di, ax
    mov si, br_s_ctxcopy
    call OSAPI_FONT_WIDTH
    cmp ax, di
    jb .measurelinkdl
    mov di, ax
.measurelinkdl:
    mov si, br_s_ctxdl
    call OSAPI_FONT_WIDTH
    cmp ax, di
    jae .measurepad
    mov ax, di
    jmp .measurepad
.measurecopy:
    mov si, br_s_ctxselcopy
    call OSAPI_FONT_WIDTH
.measurepad:
    add ax, BR_CTXPAD
    mov [br_ctxw], al
.place:
    mov cx, [br_ctxpx]
    mov ax, cx
    sub ax, [br_cx]
    jns .xok
    xor ax, ax
.xok:
    add ax, BR_CTXGAP              ; open right of the pointer when possible
    mov bx, [br_cols]               ; the page band ends before the scroll bar
    mov cl, 3
    shl bx, cl
    xor dx, dx
    mov dl, [br_ctxw]
    sub bx, dx
    jns .xmax
    xor bx, bx
.xmax:
    cmp ax, bx
    jbe .xstore
    mov ax, [br_ctxpx]              ; open leftward when the menu would cross
    sub ax, [br_cx]                 ; the viewport's right edge, leaving room
    xor dx, dx
    mov dl, [br_ctxw]
    sub ax, dx
    sub ax, BR_CTXGAP
    jns .xbound
    xor ax, ax
.xbound:
    cmp ax, bx
    jbe .xstore
    mov ax, bx
.xstore:
    mov [br_ctxx], ax
    mov ax, [br_ctxpy]
    sub ax, [br_cy]
    jns .yok
    xor ax, ax
.yok:
    add ax, BR_CTXGAP               ; and below the pointer when it fits
    mov bx, [br_rows]               ; only complete painted rows can erase it
    mov cl, 3
    shl bx, cl
    xor dx, dx
    mov dl, [br_ctxh]
    sub bx, dx
    jns .ymax
    xor bx, bx
.ymax:
    cmp ax, bx
    jbe .yset
    mov ax, [br_ctxpy]
    sub ax, [br_cy]
    sub ax, BR_CTXGAP
    jns .yup
    xor ax, ax
.yup:
    sub ax, [br_ctxh]
    jns .yflip
    xor ax, ax
.yflip:
    cmp ax, bx
    jbe .yset
    mov ax, bx
.yset:
    mov [br_ctxy], ax
.draw:
    mov byte [br_ctxopen], 1
    call br_cx_ctxdraw
.clipfail:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Draw the selected popup size. Coordinates are relative to the band so the
; window may move without a saved screen rectangle becoming stale.

br_cx_ctxdraw:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, [br_cx]
    add ax, [br_ctxx]
    mov [br_ctxpx], ax
    mov bx, [br_cy]
    add bx, [br_ctxy]
    mov [br_ctxpy], bx
    mov al, CWHITE
    call OSAPI_SET_COLOR
    ; GFX_FILL's callback clip path widens the white rectangle to the clip's
    ; left edge. Paint only the interior rows with explicit bounded spans, so
    ; text before the popup is left untouched. The black frame follows below.
    mov si, [br_ctxpy]
    inc si
    mov di, [br_ctxpy]
    xor ax, ax
    mov al, [br_ctxh]
    dec ax
    add di, ax
.fillrow:
    mov ax, [br_ctxpx]
    inc ax
    mov bx, [br_ctxpx]
    xor cx, cx
    mov cl, [br_ctxw]
    sub cx, 2
    add bx, cx
    mov dx, si
    push si
    push di
    call OSAPI_GFX_HLINE
    pop di
    pop si
    inc si
    cmp si, di
    jb .fillrow
    mov al, CBLACK
    call OSAPI_SET_COLOR
    ; Draw the four menu edges from their explicit endpoints. gfx_frame's
    ; clipped outline was extending its horizontal strips to the clip's left
    ; edge on this callback path, even though the saved popup rect was narrow.
    mov ax, [br_ctxpx]
    mov bx, ax
    xor cx, cx
    mov cl, [br_ctxw]
    dec cx
    add bx, cx
    mov dx, [br_ctxpy]
    call OSAPI_GFX_HLINE
    mov ax, [br_ctxpx]
    mov bx, ax
    xor cx, cx
    mov cl, [br_ctxw]
    dec cx
    add bx, cx
    mov dx, [br_ctxpy]
    xor cx, cx
    mov cl, [br_ctxh]
    dec cx
    add dx, cx
    call OSAPI_GFX_HLINE
    mov ax, [br_ctxpx]
    mov bx, [br_ctxpy]
    mov dx, bx
    xor cx, cx
    mov cl, [br_ctxh]
    dec cx
    add dx, cx
    call OSAPI_GFX_VLINE
    mov ax, [br_ctxpx]
    xor bx, bx
    mov bl, [br_ctxw]
    dec bx
    add ax, bx
    mov bx, [br_ctxpy]
    mov dx, bx
    xor cx, cx
    mov cl, [br_ctxh]
    dec cx
    add dx, cx
    call OSAPI_GFX_VLINE
    mov cx, [br_ctxpx]
    add cx, 2
    mov dx, [br_ctxpy]
    cmp byte [br_ctxkind], 2
    jne .tworow
    add dx, 4
    mov si, br_s_ctxselcopy
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    jmp .out
.tworow:
    add dx, 2
    cmp byte [br_ctxkind], 0
    jne .link
    mov si, br_s_ctxsave
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    mov cx, [br_ctxpx]
    add cx, 2
    mov dx, [br_ctxpy]
    add dx, 14
    cmp byte [br_viewsrc], 0
    jne .close_source
    mov si, br_s_ctxview
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    jmp .out
.close_source:
    mov si, br_s_ctxclose
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    jmp .out
.link:
    mov si, br_s_ctxopen
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    mov cx, [br_ctxpx]
    add cx, 2
    mov dx, [br_ctxpy]
    add dx, 14
    mov si, br_s_ctxcopy
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    mov cx, [br_ctxpx]
    add cx, 2
    mov dx, [br_ctxpy]
    add dx, 26
    mov si, br_s_ctxdl
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; CF=1 always: the popup owns this click, including an outside click which
; dismisses it and lets the next deliberate click start a fresh action.

br_cx_ctxclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, [br_win]
    call OSAPI_WM_CLIP_SET
    jc .consumed
    mov ax, [br_cx]
    add ax, [br_ctxx]
    cmp cx, ax
    jb .dismiss
    xor bx, bx
    mov bl, [br_ctxw]
    dec bx
    add ax, bx
    cmp cx, ax
    ja .dismiss
    mov ax, [br_cy]
    add ax, [br_ctxy]
    cmp dx, ax
    jb .dismiss
    xor bx, bx
    mov bl, [br_ctxh]
    dec bx
    add ax, bx
    cmp dx, ax
    ja .dismiss
    cmp byte [br_ctxkind], 2
    je .copyselection
    mov ax, [br_cy]
    add ax, [br_ctxy]
    add ax, 13
    cmp dx, ax
    jb .first
    cmp byte [br_ctxkind], 1
    jne .second
    add ax, 13
    cmp dx, ax
    jb .second
    mov al, 2
    jmp .action
.second:
    mov al, 1
    jmp .action
.first:
    xor al, al
.action:
    mov [br_ctxclickitem], al
    mov byte [br_ctxopen], 0
    call br_cx_ctxerase
    call far [br_dl_r_paint_band]
    call far [br_dl_r_sbar]
    cmp byte [br_ctxkind], 0
    jne .link
    cmp byte [br_ctxclickitem], 0
    jne .source
    call far [br_dl_r_saveas]
    jmp .consumed
.source:
    call br_cx_srctoggle
    jmp .consumed
.link:
    cmp byte [br_ctxclickitem], 2
    je .download
    cmp byte [br_ctxclickitem], 0
    jne .copylink
    mov ax, [br_ctxlink]
    call far [br_dl_r_follow]
    jmp .consumed
.copylink:
    call br_cx_ctxcopylink
    jmp .consumed
.download:
    mov ax, [br_ctxlink]
    call br_dlx_link
    jmp .consumed
.copyselection:
    mov byte [br_ctxopen], 0
    call br_cx_ctxerase
    call far [br_dl_r_paint_band]
    call far [br_dl_r_sbar]
    call br_copyselection
    jmp .consumed
.dismiss:
    mov byte [br_ctxopen], 0
    call br_cx_ctxerase
    call far [br_dl_r_paint_band]
    call far [br_dl_r_sbar]
.consumed:
    stc
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; GFX_FRAME is an XOR outline. Blank the popup's recorded rectangle before
; repainting the document so no old menu width or edge survives a replacement.

br_cx_ctxerase:
    push ax
    push bx
    push cx
    push dx
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [br_cx]
    add ax, [br_ctxx]
    mov bx, [br_cy]
    add bx, [br_ctxy]
    mov cx, ax
    xor dx, dx
    mov dl, [br_ctxw]
    dec dx
    add cx, dx
    mov dx, bx
    xor ax, ax
    mov al, [br_ctxh]
    dec ax
    add dx, ax
    call OSAPI_GFX_FILL
    pop dx
    pop cx
    pop bx
    pop ax
    ret


br_cx_ctxcopylink:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov bx, [br_ctxlink]
    cmp bx, [br_lnkn]
    jae .out
    shl bx, 1
    mov si, [br_lnkoff+bx]
    mov es, [br_lnkseg]
    xor cx, cx
.len:
    cmp cx, BR_UBUF
    jae .put
    cmp byte [es:si], 0
    je .put
    inc si
    inc cx
    jmp .len
.put:
    sub si, cx
    call OSAPI_CLIP_PUT
    jc .out
    mov word [br_nmsg], br_s_copied
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret


; The three below write the records in the URL store, so they refuse (CF=1)
; with no store: br_bmrefresh claims it first, and its refusal is theirs.
; ES is the store on the way out of the first two.
br_cx_bmaddmem:
    cmp word [br_urlseg], 0
    je .full
    mov es, [br_urlseg]
    xor bx, bx
    mov bl, [es:BR_BMCOUNT]
    cmp bl, BR_BMMAX
    jae .full
    mov ax, bx
    mov di, BR_BMRECSZ
    mul di
    add ax, BR_BMREC
    mov di, ax
    mov si, br_bmreqtitle
    cmp byte [si], 0
    jne .title
    mov si, br_bmrequrl
.title:
    mov cx, BR_BMTITLE
    call far [br_dl_r_bcopyz]
    mov ax, bx
    mov di, BR_BMRECSZ
    mul di
    add ax, BR_BMURLBASE
    mov di, ax
    mov si, br_bmrequrl
.url:
    mov cx, BR_BMURL
    call far [br_dl_r_bcopyz]
    inc byte [es:BR_BMCOUNT]
    clc
    ret
.full:
    stc
    ret


br_cx_bmremove:
    mov si, br_bmrequrl
    call far [br_dl_r_bmfindurl]        ; CF=1 with no store as well
    jc .out
    mov es, [br_urlseg]
    mov bx, ax
    inc bx
    cmp bl, [es:BR_BMCOUNT]
    jae .last
    mov ax, bx
    mov di, BR_BMRECSZ
    mul di
    add ax, BR_BMREC
    mov si, ax
    dec bx
    mov ax, bx
    mov di, BR_BMRECSZ
    mul di
    add ax, BR_BMREC
    mov di, ax
    xor ax, ax
    mov al, [es:BR_BMCOUNT]
    sub ax, bx
    dec ax
    mov dx, BR_BMRECSZ
    mul dx
    mov cx, ax
    push ds
    push es
    pop ds                          ; both ends are the store's
    cld
    rep movsb
    pop ds
.last:
    dec byte [es:BR_BMCOUNT]
    clc
    ret
.out:
    ret


br_cx_bmwrite:
    cmp word [br_urlseg], 0
    jne .store
    stc
    ret
.store:
    mov es, [br_urlseg]
    mov si, br_s_bmfile
    mov bx, BR_URLBM
    ; Persist only live records. A fixed-capacity write was 1,477 bytes even
    ; for one bookmark (three sectors on the 360KB disk), keeping the whole OS
    ; in the disk driver's synchronous transfer path for needless I/O.
    xor ax, ax
    mov al, [es:BR_BMCOUNT]
    mov di, BR_BMRECSZ
    mul di
    add ax, BR_BMHEADER
    mov cx, ax
    mov di, br_bmstamp              ; ES:BX the store's image, DI our stamp
    xor ax, ax
    call far [br_dl_r_pagepin]          ; pinned for the call, as br_bmrefresh
    call far [br_dl_r_odwrite]
    call far [br_dl_r_pageunpin]        ; AX and the flags kept
    ret


; --- View Source / Close Source (SPEC.md 71.24) ------------------------------
; The raw source projected into the document claim, a line break as the
; Browser's own D_BR. It reads the source through br_srcptr and writes the
; document through br_obuf and br_flushbuf, so it is right past 64 KiB, in EMS
; and under kern_mega - where it read [br_srcseg]:SI and wrote [br_docseg]:DI
; with 16-bit offsets, sized the document as [br_dockb] << 10 (which wraps at
; 64 KiB) and ignored the source's high word.
;
; The source and the document share EMS's frame, so a chunk of source is read
; with the output buffer EMPTY: a chunk is at most BR_SRCCHUNK bytes, each
; emits at most one, and br_obuf takes 250 before br_put would flush - so
; nothing remaps the frame under ES:SI until the chunk is done and
; br_flushbuf writes it out.
BR_SRCCHUNK equ 248
br_cx_srctoggle:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, [br_srclen]
    or ax, [br_srclen_hi]
    jz .none
    cmp word [br_srcseg], 0
    jne .have
    cmp word [br_srcemshnd], 0
    je .none
.have:
    cmp byte [br_viewsrc], 0
    jne .rendered
    mov byte [br_viewsrc], 1
    mov word [br_doclen], 0
    mov word [br_doclen_hi], 0
    mov byte [br_on], 0
    mov byte [br_docfull], 0
    xor ax, ax                      ; DX:AX = the source position
    xor dx, dx
    xor bl, bl                      ; BL = 1: the last byte was a CR
.chunk:
    cmp dx, [br_srclen_hi]
    jb .more
    ja .endraw
    cmp ax, [br_srclen]
    jae .endraw
.more:
    call far [br_dl_r_flushbuf]     ; empty, before the source is mapped
    cmp byte [br_docfull], 0
    jne .endraw                     ; the document is at its ceiling
    call far [br_dl_r_srcptr]       ; ES:SI, [br_srcwinlen] its end, and
    mov cx, [br_srcwinlen]          ; [br_spoint] the position asked for
    sub cx, si                      ; what the window still reaches
    jbe .endraw                     ; (a refused map reaches nothing)
    cmp cx, BR_SRCCHUNK
    jbe .lim
    mov cx, BR_SRCCHUNK
.lim:
    mov ax, [br_srclen]             ; ...and what the source still has,
    sub ax, [br_spoint]             ; which is at least one here
    mov dx, [br_srclen_hi]
    sbb dx, [br_spoint+2]
    jnz .left                       ; 64 KiB or more: the chunk stands
    cmp ax, cx
    jae .left
    mov cx, ax
.left:
    mov ax, [br_spoint]
    mov dx, [br_spoint+2]
    add ax, cx                      ; the position after this chunk
    adc dx, 0
.byte:
    push ax
    mov al, [es:si]
    inc si
    cmp al, 10
    jne .notlf
    or bl, bl                       ; the LF of a CR LF: one break, which
    mov bl, 0                       ; the CR already put
    jnz .next
    jmp short .line
.notlf:
    mov bl, 0
    cmp al, 13
    jne .notcr
    mov bl, 1
.line:
    mov al, D_BR
    jmp short .emit
.notcr:
    cmp al, 9
    jne .ctl
    mov al, ' '
.ctl:
    cmp al, 32
    jb .replace
    cmp al, 126
    jbe .emit
.replace:
    mov al, '?'
.emit:
    push bx
    mov bl, [br_on]
    xor bh, bh
    mov [bx+br_obuf], al            ; br_put's store, without its flush: the
    inc byte [br_on]                ; chunk cannot fill br_obuf
    pop bx
.next:
    pop ax
    loop .byte
    jmp .chunk
.endraw:
    cmp byte [br_docfull], 0
    jne .ended                      ; br_docensure refused: truncated, and the
    push bx                         ; D_END is the one byte it cannot take
    mov bl, [br_on]
    xor bh, bh
    mov byte [bx+br_obuf], D_END
    inc byte [br_on]
    pop bx
    call far [br_dl_r_flushbuf]
.ended:
    jmp .layout
.rendered:
    mov byte [br_viewsrc], 0
    mov byte [br_parse_started], 0  ; A FRESH PARSE, not a resume: br_parse
    mov byte [br_parse_final], 1    ; resumes at the saved source cursor when
                                    ; this is set, and that cursor is at the
                                    ; END - so Close Source re-parsed nothing
                                    ; and left the raw-source projection as
                                    ; the document, with every form and link
                                    ; record pointing into it
    call far [br_dl_r_parse]
.layout:
    mov word [br_cols], 0
    mov word [br_top], 0
    mov word [br_ptop], 0
    mov si, [br_win]
    call far [br_dl_r_measure]
    call far [br_dl_r_paint_band]
    call far [br_dl_r_sbar]
    call far [br_dl_r_settitle]
    jmp short .out
.none:
    mov word [br_nmsg], br_s_srcnone
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- br_cx_bmcommit - one queued bookmark operation against the records ------
; br_bmservice's commit, after its refresh has read the file and br_bmfindurl
; has set [br_bmsaved] for the queued URL: add or remove, then write.
; out: CF=0 and [br_nmsg] says which; CF=1 failed (br_bmservice re-reads and
; says so). Clobbers AX, BX, CX, DX, SI, DI, ES
br_cx_bmcommit:
    cmp byte [br_bmopwork], 1
    jne .remove
    cmp byte [br_bmsaved], 0
    jne .saved                      ; already in the file: it IS saved, and
    call br_cx_bmaddmem             ; the request's "Saving..." must not stand
    jc .failed
    call br_cx_bmwrite
    jc .failed
.saved:
    mov word [br_nmsg], br_s_bmsaved
    clc
    ret
.remove:
    cmp byte [br_bmsaved], 0
    je .removed                     ; not in the file: it IS removed
    call br_cx_bmremove
    call br_cx_bmwrite
    jc .failed
.removed:
    mov word [br_nmsg], br_s_bmremoved
    clc
    ret
.failed:
    stc
    ret

; --- br_cx_submit - compose the query and say what it would fetch ---------------
; BROWSER-PLAN 7.4. With no transport yet the URL is BUILT and REPORTED rather
; than fetched, which is exactly what 10 step 4 asks for: the encoder is
; checkable by printing the URL a submit would produce.
br_cx_submit:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov [br_submitform], al
    xor ah, ah
    mov bx, ax
    shl bx, 1
    mov si, [br_formtarget+bx]
    mov es, [br_lnkseg]
    mov dl, [br_formowner+bx]
    call far [br_dl_r_targetroute]
    jc .outes
.targetok:
    xor ah, ah
    mov bx, ax
    mov al, [bx+br_fmethods]
    mov [br_post], al               ; one-shot method consumed by br_go
    mov word [br_un], 0
    mov byte [br_url], 0
    mov byte [br_urlcut], 0
    mov byte [br_dest], 1
    cmp byte [br_post], 0
    jne .q
    xor ah, ah
    mov bl, BR_ACTMAX
    mul bl
    mov si, ax
    add si, br_factions             ; this submit button's action, verbatim
.act:
    mov al, [si]
    or al, al
    jz .q
    call far [br_dl_r_uput]
    inc si
    jmp .act
.q:
    cmp byte [br_post], 0
    jne .fields
    mov al, '?'
    call far [br_dl_r_uput]
.fields:
    xor cx, cx                      ; CX = field index
    mov byte [br_pair], 0            ; mapping a dword value also uses DX
.fld:
    cmp cl, [br_fn]
    jae .button
    mov al, cl
    call far [br_dl_r_fld]
    mov al, [br_submitform]
    cmp [bx+FLD_FORM], al
    jne .nextf
    cmp byte [bx+FLD_TYPE], 2
    jb .named
    cmp byte [bx+FLD_LEN], 0
    je .nextf
.named:
    cmp byte [bx+FLD_NAME], 0
    je .nextf
    cmp byte [br_pair], 0
    jz .amp0
    mov al, '&'
    call far [br_dl_r_uput]
.amp0:
    mov byte [br_pair], 1
    push cx
    mov si, bx
    add si, FLD_NAME
.nm:
    mov al, [si]
    or al, al
    jz .eq
    call far [br_dl_r_encb]
    inc si
    jmp .nm
.eq:
    mov al, '='
    call far [br_dl_r_uput]
    cmp byte [bx+FLD_TYPE], 1
    je .choiceval
    cmp byte [bx+FLD_TYPE], 2
    jae .choiceval
.textval:
    mov ax, [bx+FLD_OFS]
    mov dx, [bx+FLD_OFS+2]
    call far [br_dl_r_docmap]
    mov si, di
    mov cl, [bx+FLD_LEN]
    xor ch, ch
.val:
    or cx, cx
    jz .doneval
    mov al, [es:si]
    call far [br_dl_r_encb]
    inc si
    dec cx
    jmp .val
.choiceval:
    mov si, bx
    add si, FLD_VALUE
.cv:
    mov al, [si]
    or al, al
    jz .doneval
    call far [br_dl_r_encb]
    inc si
    jmp .cv
.doneval:
    pop cx
.nextf:
    inc cl
    jmp .fld
.button:                            ; --- the CLICKED submit button, alone ---
    mov al, [br_subhit]             ; 0FFh: Return in a field, no button
    cmp al, 0FFh
    je .hid
    xor ah, ah
    mov bl, BR_SUBSZ
    mul bl
    mov bx, ax
    add bx, br_subs
    mov ax, [bx+SUB_AEND]
    sub ax, [bx+SUB_ASTART]
    mov dx, [bx+SUB_AEND+2]
    sbb dx, [bx+SUB_ASTART+2]
    or dx, dx                       ; one tag is bounded by the source window
    jnz .hid
    mov [br_fattrlen], ax
    mov ax, [bx+SUB_ASTART]
    mov dx, [bx+SUB_ASTART+2]
    call far [br_dl_r_srcptr]           ; remap the retained source after parsing
    mov [br_astart], si
    add si, [br_fattrlen]
    mov [br_aend], si
    mov di, br_a_name
    call far [br_dl_r_getattr]
    jc .hid                         ; an unnamed button is not a control
    cmp byte [br_pair], 0
    jz .bamp0
    mov al, '&'
    call far [br_dl_r_uput]
.bamp0:
    mov byte [br_pair], 1
    mov dx, 1
    call far [br_dl_r_henc]             ; the name, percent-encoded
    mov al, '='
    call far [br_dl_r_uput]
    mov di, br_a_value
    call far [br_dl_r_getattr]
    jc .hid
    call far [br_dl_r_henc]
.hid:
    xor ax, ax
    mov al, [br_submitform]
    mov bx, ax
    shl bx, 1
    mov cx, [br_hnws+bx]
    jcxz .say
    mov bx, BR_HIDMAX
    push dx                          ; preserve scratch across the table index MUL
    mul bx
    pop dx
    mov si, ax
    add si, br_hids
    cmp byte [br_pair], 0
    jz .hloop
    mov al, '&'
    call far [br_dl_r_uput]
    mov byte [br_pair], 1
.hloop:
    jcxz .say
    mov al, [si]
    call far [br_dl_r_uput]             ; already encoded at parse time
    inc si
    dec cx
    jmp .hloop
.say:
    mov byte [br_dest], 0
    cmp byte [br_urlcut], 0
    je .show
    cmp byte [br_post], 0
    jne .posttoo
    mov si, br_s_urltoo
    jmp .too
.posttoo:
    mov si, br_s_datatoo
.too:
    mov byte [br_post], 0
    mov [br_nmsg], si
    mov byte [br_nstate], BN_ERR
    call far [br_dl_r_status]
    pop es
    jmp .out
.show:
    cmp byte [br_post], 0
    jne .posttoast
    call far [br_dl_r_toasturl]     ; (the primary's: its strings are there)
    mov si, br_url
    call br_cx_formresolve
    jmp .resolved
.posttoast:
    call far [br_dl_r_toastpost]
    mov al, [br_submitform]
    xor ah, ah
    mov bl, BR_ACTMAX
    mul bl
    mov si, ax
    add si, br_factions             ; resolve the action without the body/query
    call br_cx_formresolve
.resolved:
    pop es
    jc .clearpost
                                    ; into a page (NET-STACK-PLAN stage D').
                                    ;
                                    ; **RESOLVED FIRST, AND THAT IS NOT
                                    ; OPTIONAL**: a form's action is a URL like
                                    ; any other and is usually RELATIVE -
                                    ; FrogFind's is `/`, so the composed thing
                                    ; is `/?q=...`, which br_split rightly
                                    ; refuses as not-http. The search box could
                                    ; be typed into and never submitted
                                    ; anywhere. One call, the link resolver's,
                                    ; so an action and an href cannot come to
                                    ; disagree about what `/` means
    call far [br_dl_r_targetgo]         ; GET forms can target a composed child
    jmp short .out
.clearpost:
    mov byte [br_post], 0
.out:                               ; br_resolve refused and has already said
    pop di                          ; why on the status line
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.outes:                             ; br_targetroute refused, ES still pushed
    pop es
    jmp short .out

; Resolve a form action against the BASE HREF saved for its owning document.
; The host/path globals are borrowed only for this resolution and restored on
; both success and refusal so later links keep the page's original base.

br_cx_formresolve:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov [br_formsrc], si
    push ds
    pop es
    mov si, br_host
    mov di, br_formhost
    mov cx, BR_HOSTMAX
    cld
    rep movsb
    mov si, br_path
    mov di, br_formpath
    mov cx, BR_PATHMAX
    rep movsb
    mov al, [br_pendingflag]
    mov [br_formpending], al
    mov byte [br_pendingflag], 0
    xor bx, bx
    mov al, [br_submitform]
    xor ah, ah
    mov bx, ax
    mov al, [br_formowner+bx]
    or al, al
    jnz .framebase
    mov si, [br_rootbaseoff]
    jmp short .basecheck
.framebase:
    dec al
    xor bx, bx
    mov bl, al
    shl bx, 1
    mov si, [br_framebaseoff+bx]
.basecheck:
    cmp si, 0FFFFh
    je .resolve
    mov es, [br_lnkseg]
    mov di, br_ubuf
.copybase:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    test al, al
    jnz .copybase
    mov si, br_ubuf
    call far [br_dl_r_split]
    jc .badbase                    ; retained bases should already be valid
.resolve:
    push ds
    pop es                          ; actions and GET URLs live in this package
    mov si, [br_formsrc]
    call far [br_dl_r_resolve]
    pushf
    pop ax
    and ax, 1
    mov [br_formrescf], al
    jmp short .restore
.badbase:
    mov byte [br_formrescf], 1
.restore:
    push ds
    pop es
    mov si, br_formhost
    mov di, br_host
    mov cx, BR_HOSTMAX
    cld
    rep movsb
    mov si, br_formpath
    mov di, br_path
    mov cx, BR_PATHMAX
    rep movsb
    mov al, [br_formpending]
    mov [br_pendingflag], al
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    cmp byte [br_formrescf], 0
    jne .refused
    clc
    ret
.refused:
    stc
    ret
