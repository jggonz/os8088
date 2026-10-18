; =============================================================================
; Browser 1.5: source view, package-side context actions and persistent links.
; The window owns all transient UI. Bookmarks are a small fixed file in the
; package volume's SYSTEM/APPDATA directory; no kernel or network interface is
; added here.
; =============================================================================

BR_BMMAX       equ 8
BR_BMTITLE     equ 24
BR_BMURL       equ BR_UBUF
BR_BMRECSZ     equ BR_BMTITLE + BR_BMURL
BR_BMHEADER    equ 5
BR_BMFILEBYTES equ BR_BMHEADER + BR_BMMAX * BR_BMRECSZ
BR_BMURLBASE   equ br_bmdata + BR_BMHEADER + BR_BMTITLE
BR_BMREC       equ br_bmdata + BR_BMHEADER
BR_CTXPAD      equ 4               ; two pixels either side of the widest label
BR_CTXH        equ 28
BR_CTXH_COPY   equ 16
BR_CTXH_LINK   equ 40
BR_CTXGAP      equ 18              ; keep the cursor glyph clear of the frame
BR_STARW       equ 16

br_s_bmfile:   db 'BROWSE.DAT', 0
br_s_bmlocal:  db 'Cannot Bookmark Local Files', 0
br_s_bmempty:  db 'Nothing to Bookmark', 0
br_s_bmfail:   db 'Bookmark save failed', 0
br_s_bmsaved:  db 'Bookmark saved', 0
br_s_bmremoved: db 'Bookmark removed', 0
br_s_bmsaving: db 'Saving bookmark...', 0
br_s_bmremoving: db 'Removing bookmark...', 0
br_s_bmtoggling: db 'Updating bookmark...', 0
br_s_copied:   db 'Link copied', 0
br_s_ctxopen:  db 'Open Link', 0
br_s_ctxcopy:  db 'Copy Link', 0
br_s_ctxdl:    db 'Save Link As...', 0
br_s_ctxselcopy: db 'Copy', 0
br_s_ctxsave:  db 'Save Page As...', 0
br_s_ctxview:  db 'View Source', 0
br_s_ctxclose: db 'Close Source', 0
br_s_srcnone:  db 'Nothing loaded to view', 0
; 16x16 five-point star, bit 7 leftmost in each byte. The empty glyph is the
; one-pixel outline of the filled glyph; both share the same centered raster.
br_star_empty:
    db 01h,00h, 01h,00h, 03h,80h, 02h,80h
    db 02h,80h, 7Eh,0FCh, 30h,18h, 18h,30h
    db 08h,20h, 08h,20h, 0Fh,0E0h, 0Ch,60h
    db 18h,30h, 00h,00h, 00h,00h, 00h,00h
br_star_full:
    db 01h,00h, 01h,00h, 01h,00h, 03h,80h
    db 03h,80h, 7Fh,0FCh, 3Fh,0F8h, 1Fh,0F0h
    db 0Fh,0E0h, 0Fh,0E0h, 0Fh,0E0h, 0Ch,60h
    db 18h,30h, 00h,00h, 00h,00h, 00h,00h

; Clear a retained XOR overlay before another control takes focus. Redrawing
; only the old page rows keeps a click in the toolbar from leaving black bands
; behind; the address line owns its own repaint.
br_selclear:
    cmp byte [br_selkind], 1
    je .page
    cmp byte [br_selkind], 2
    jne .out
    mov byte [br_selkind], 0
    call br_ldraw
    ret
.page:
    mov byte [br_selkind], 0
    call br_selpaint
.out:
    ret

; --- A link under the pointer takes priority; otherwise a retained selection
; gets its one-item Copy popup. The shared WM right-click API remains.
br_onrclk:
    mov [br_ctxpx], cx
    mov [br_ctxpy], dx
    jmp br_ctxshow

; --- frame and place the context panel inside the current band --------------
br_ctxshow:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, [br_win]
    call br_measure
    mov bx, [br_win]
    call OSAPI_WM_CLIP_SET          ; the popup must not paint beyond this window
    jc .clipfail
    cmp byte [br_ctxopen], 0
    je .choose
    mov byte [br_ctxopen], 0
    call br_ctxerase                 ; GFX_FRAME is XOR: erase its whole old box
    call br_paint_band              ; replace an existing popup before drawing
    call br_sbar                    ; the popup must not leave marks on the bar
.choose:
    mov byte [br_ctxkind], 0
    mov byte [br_ctxh], BR_CTXH
    mov word [br_ctxlink], 0FFFFh
    ; Resolve the pointer first. A retained text selection must not mask the
    ; link-specific actions when the user right-clicks directly on a link.
.hit:
    mov cx, [br_ctxpx]
    mov dx, [br_ctxpy]
    call br_hitofs
    jc .selection
    call br_linkat
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
    call br_ctxdraw
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
br_ctxdraw:
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
br_ctxclick:
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
    call br_ctxerase
    call br_paint_band
    call br_sbar
    cmp byte [br_ctxkind], 0
    jne .link
    cmp byte [br_ctxclickitem], 0
    jne .source
    call br_saveas
    jmp .consumed
.source:
    call br_sourcetoggle
    jmp .consumed
.link:
    cmp byte [br_ctxclickitem], 2
    je .download
    cmp byte [br_ctxclickitem], 0
    jne .copylink
    mov ax, [br_ctxlink]
    call br_follow
    jmp .consumed
.copylink:
    call br_ctxcopylink
    jmp .consumed
.download:
    mov ax, [br_ctxlink]
    call br_dl_link
    jmp .consumed
.copyselection:
    mov byte [br_ctxopen], 0
    call br_ctxerase
    call br_paint_band
    call br_sbar
    call br_copyselection
    jmp .consumed
.dismiss:
    mov byte [br_ctxopen], 0
    call br_ctxerase
    call br_paint_band
    call br_sbar
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
br_ctxerase:
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

br_ctxcopylink:
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

; --- bookmark menu and persistence ------------------------------------------
br_bmrefresh:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    mov bx, br_bmdata
    mov cx, BR_BMFILEBYTES
    xor dx, dx
    mov si, br_s_bmfile
    call od_read
    jc .empty
    or dx, dx
    jnz .empty
    cmp ax, BR_BMHEADER
    jb .empty
    cmp byte [br_bmdata], 'B'
    jne .empty
    cmp byte [br_bmdata+1], 'M'
    jne .empty
    cmp byte [br_bmdata+2], '0'
    jne .empty
    cmp byte [br_bmdata+3], '1'
    jne .empty
    cmp byte [br_bmdata+4], BR_BMMAX
    ja .empty
    xor bx, bx
    mov [br_copycap], ax
    mov bl, [br_bmdata+4]
    mov ax, BR_BMRECSZ
    mul bx
    add ax, BR_BMHEADER
    cmp ax, [br_copycap]
    ja .empty
    jmp .done
.empty:
    push es
    push ds
    pop es
    mov di, br_bmdata
    xor ax, ax
    mov cx, BR_BMFILEBYTES/2
    rep stosw
    mov byte [br_bmdata], 'B'
    mov byte [br_bmdata+1], 'M'
    mov byte [br_bmdata+2], '0'
    mov byte [br_bmdata+3], '1'
    pop es
.done:
    call br_bmmenuset
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

br_bmmenuset:
    push ax
    push bx
    push cx
    push di
    push es
    push ds
    pop es
    xor cx, cx
    mov cl, [br_bmdata+4]           ; count is a byte; the next byte is title text
    jcxz .none
    xor bx, bx
.item:
    mov ax, bx
    mov di, BR_BMRECSZ
    mul di
    add ax, BR_BMREC
    mov di, bx
    shl di, 1
    mov [br_bmitems+di], ax
    inc bx
    loop .item
    mov [BR_BMNITEM], bx
    jmp .out
.none:
    mov word [br_bmitems], br_it_bmnone
    mov word [BR_BMNITEM], 1
.out:
    pop es
    pop di
    pop cx
    pop bx
    pop ax
    ret

; AL = menu item -> SI = its URL. Also used by the real menu callback.
br_bmurlptr:
    push ax
    push bx
    push cx
    mov bl, al
    xor bh, bh
    mov ax, BR_BMRECSZ
    mul bx
    add ax, BR_BMURLBASE
    mov si, ax
    pop cx
    pop bx
    pop ax
    ret

; Search the current canonical URL. CF=0 and AX=index when found.
br_bmfind:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    xor dx, dx
    mov dl, [br_bmdata+4]           ; count is a byte, not a word
    xor bx, bx
.next:
    cmp bx, dx
    jae .miss
    mov ax, bx
    mov di, BR_BMRECSZ
    mul di
    add ax, BR_BMURLBASE
    mov di, ax
    push ds
    pop es
    mov si, br_ubuf
.cmp:
    mov al, [si]
    mov ah, [es:di]
    call br_bmupper
    xchg al, ah
    call br_bmupper
    xchg al, ah
    cmp al, ah
    jne .different
    or al, al
    jz .found
    inc si
    inc di
    jmp .cmp
.different:
    inc bx
    jmp .next
.found:
    mov ax, bx
    clc
    jmp .out
.miss:
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

br_bmupper:
    cmp al, 'a'
    jb .out
    cmp al, 'z'
    ja .out
    sub al, 20h
.out:
    ret

br_bmcheck:
    mov byte [br_bmsaved], 0
    cmp byte [br_islocal], 0
    jne .out
    call br_bmfind
    jc .out
    mov byte [br_bmsaved], 1
.out:
    ret

br_starrect:
    push ax
    push bx
    mov ax, [br_cx]
    add ax, [br_cw]
    sub ax, BR_LPAD + BR_STARW
    sub ax, [br_cx]
    and ax, 0FFF8h                  ; BLIT1 is byte-aligned on kern_small too
    add ax, [br_cx]
    mov [br_starx1], ax
    mov bx, ax
    add bx, BR_STARW - 1
    mov [br_starx2], bx
    mov ax, [br_tby]
    mov [br_stary1], ax
    add ax, BR_TBH - 1
    mov [br_stary2], ax
    pop bx
    pop ax
    ret

br_stardraw:
    cmp byte [br_islocal], 0
    jne .out
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    push bp
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_GFX_BLIT1_PEN
    push ds
    pop es
    cmp byte [br_bmsaved], 0
    jne .filled
    mov si, br_star_empty
    jmp .draw
.filled:
    mov si, br_star_full
.draw:
    mov bp, 2
    mov ax, [br_starx1]
    mov bx, [br_stary1]
    mov cx, 16
    mov dx, 16
    call OSAPI_GFX_BLIT1
    pop bp
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.out:
    ret

; UI callbacks only queue bookmark work. File access happens in br_bmservice,
; reached from W_ONWAKE with the graphics lock released.
br_bmtoggle:
    cmp byte [br_islocal], 0
    jne .local
    ; Decide add/remove only after br_bmservice has re-read the shared file.
    ; br_bmsaved can lag while navigation's refresh wake is still pending.
    mov al, 4
    call br_bmrequest
    ret
.local:
    mov word [br_nmsg], br_s_bmlocal
    mov byte [br_nstate], BN_DONE
    call br_status
    ret

br_bmadd:
    cmp byte [br_islocal], 0
    je .remote
    mov word [br_nmsg], br_s_bmlocal
    mov byte [br_nstate], BN_DONE
    call br_status
    ret
.remote:
    cmp byte [br_ubuf], 0
    jne .hasurl
    mov word [br_nmsg], br_s_bmempty
    mov byte [br_nstate], BN_DONE
    call br_status
    ret
.hasurl:
    mov al, 1
    call br_bmrequest
    ret
.already:
    ret

; AL = 1 ensure saved, 2 ensure removed, 3 refresh cache only.
br_bmrequest:
    cmp al, 3
    je .refresh
    mov [br_bmreq], al
    cmp al, 4
    je .toggle
    cmp al, 1
    jne .wantremove
    mov byte [br_bmsaved], 1       ; immediate feedback while the file is saved
    mov word [br_nmsg], br_s_bmsaving
    jmp .wantdraw
.toggle:
    mov word [br_nmsg], br_s_bmtoggling
    jmp .wantdraw
.wantremove:
    mov byte [br_bmsaved], 0
    mov word [br_nmsg], br_s_bmremoving
.wantdraw:
    mov byte [br_nstate], BN_DONE
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call br_stardraw               ; callbacks already own the gfx lock
    call br_status
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    push ax
    push cx
    push si
    push di
    push es
    push ds
    pop es
    mov si, br_ubuf
    mov di, br_bmrequrl
    mov cx, BR_UBUF
    call br_copyz
    mov si, br_ptitle
    mov di, br_bmreqtitle
    mov cx, BR_BMTITLE
    call br_copyz
    pop es
    pop di
    pop si
    pop cx
    pop ax
    jmp .post
.post:
    call br_bmpost
    ret
.refresh:
    mov byte [br_bmrefreshreq], 1
    jmp .post

; Called only from locked UI callbacks; W_ONWAKE records the redraw debt.
br_bmredrawnow:
    cmp byte [br_bmredraw], 0
    je .out
    mov byte [br_bmredraw], 0
    call br_stardraw
    call br_status
.out:
    ret

; UI task only: refresh the small cache and commit one requested operation.
br_bmpost:
    cmp byte [br_bmreq], 0
    jne .post
    cmp byte [br_bmrefreshreq], 0
    je .out
.post:
    push bx
    mov bx, [br_win]
    call OSAPI_WM_WAKE
    pop bx
    jnc .out
%ifdef OS88UI_SBDRAG
    ; A full event ring refuses the wake. Do not wait for an unrelated
    ; click or paint: the timer callback retries once the UI has drained it.
    mov byte [br_bmretry], 1
    mov bx, [br_win]
    mov ax, 1
    call OSAPI_WM_TIMER
%endif
.out:
    ret

; W_ONWAKE only: disk read, reconcile one queued operation, then disk write.
; This keeps the floppy path out of mouse/key callbacks and their gfx lock.
br_bmservice:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov al, [br_bmreq]
    mov [br_bmopwork], al
    or al, al
    jnz .work
    cmp byte [br_bmrefreshreq], 0
    je .out
    mov byte [br_bmopwork], 3
.work:
    mov byte [br_bmrefreshreq], 0
    call br_bmrefresh
    mov al, [br_bmreq]
    or al, al
    jz .refresh_only
    mov byte [br_bmreq], 0
    push ds
    pop es
    mov si, br_ubuf
    mov di, br_bmworkurl
    mov cx, BR_UBUF
    call br_copyz
    mov si, br_bmrequrl
    mov di, br_ubuf
    mov cx, BR_UBUF
    call br_copyz
    call br_bmcheck
    cmp byte [br_bmopwork], 4
    jne .decide
    cmp byte [br_bmsaved], 0
    je .toggle_add
    mov byte [br_bmopwork], 2
    jmp .decide
.toggle_add:
    mov byte [br_bmopwork], 1
.decide:
    cmp byte [br_bmopwork], 1
    jne .remove
    cmp byte [br_bmsaved], 0
    jne .restore
    call br_bmaddmem
    jc .failed
    call br_bmwrite
    jc .failed
    mov word [br_nmsg], br_s_bmsaved
    jmp .success
.remove:
    cmp byte [br_bmsaved], 0
    je .restore
    call br_bmremove
    call br_bmwrite
    jc .failed
    mov word [br_nmsg], br_s_bmremoved
.success:
    mov byte [br_nstate], BN_DONE
    jmp .restore
.failed:
    call br_bmrefresh
    mov word [br_nmsg], br_s_bmfail
    mov byte [br_nstate], BN_DONE
.restore:
    push ds
    pop es
    mov si, br_bmworkurl
    mov di, br_ubuf
    mov cx, BR_UBUF
    call br_copyz
.refresh_only:
    call br_bmcheck
    call br_bmmenuset
    mov byte [br_bmredraw], 1      ; repaint after the disk operation returns
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

br_bmaddmem:
    xor bx, bx
    mov bl, [br_bmdata+4]
    cmp bl, BR_BMMAX
    jae .full
    xor bx, bx
    mov bl, [br_bmdata+4]
    mov ax, bx
    mov di, BR_BMRECSZ
    mul di
    add ax, BR_BMREC
    mov di, ax
    push ds
    pop es
    mov si, br_bmreqtitle
    cmp byte [si], 0
    jne .title
    mov si, br_ubuf
.title:
    mov cx, BR_BMTITLE - 1
    call br_copyz
    mov ax, bx
    mov di, BR_BMRECSZ
    mul di
    add ax, BR_BMURLBASE
    mov di, ax
    mov si, br_ubuf
.url:
    mov cx, BR_BMURL - 1
    call br_copyz
    inc byte [br_bmdata+4]
    clc
    ret
.full:
    stc
    ret

br_bmremove:
    call br_bmfind
    jc .out
    mov bx, ax
    inc bx
    cmp bl, [br_bmdata+4]
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
    mov al, [br_bmdata+4]
    sub ax, bx
    dec ax
    mov dx, BR_BMRECSZ
    mul dx
    mov cx, ax
    push ds
    pop es
    cld
    rep movsb
.last:
    dec byte [br_bmdata+4]
    clc
    ret
.out:
    ret

br_bmwrite:
    mov si, br_s_bmfile
    mov bx, br_bmdata
    ; Persist only live records. A fixed-capacity write was 1,477 bytes even
    ; for one bookmark (three sectors on the 360KB disk), keeping the whole OS
    ; in the disk driver's synchronous transfer path for needless I/O.
    xor ax, ax
    mov al, [br_bmdata+4]
    mov di, BR_BMRECSZ
    mul di
    add ax, BR_BMHEADER
    mov cx, ax
    push ds
    pop es
    mov di, br_bmstamp
    call od_write
    ret

br_copyz:
    push ax
    push cx
    push di
    push si
    xor al, al
    cld
.copy:
    jcxz .end
    lodsb
    or al, al
    jz .zero
    stosb
    loop .copy
    jmp .done
.zero:
    stosb
    dec cx
.zeroes:
    jcxz .done
    stosb
    loop .zeroes
.end:
    stosb
.done:
    pop si
    pop di
    pop cx
    pop ax
    ret

; --- retained source view; raw bytes are projected into the existing document
; claim with line breaks represented by the Browser's normal D_BR marker.
br_sourcetoggle:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cmp word [br_srcseg], 0
    je .none
    cmp word [br_srclen], 0
    je .none
    cmp byte [br_viewsrc], 0
    jne .rendered
    mov byte [br_viewsrc], 1
    mov word [br_doclen], 0
    mov ax, [br_srcseg]
    mov [br_ctxpx], ax
    mov ax, [br_docseg]
    mov [br_ctxpy], ax
    mov ax, [br_dockb]
    mov cl, 10
    shl ax, cl
    dec ax
    mov [br_copycap], ax
    xor si, si
    xor di, di
.raw:
    cmp si, [br_srclen]
    jae .endraw
    cmp di, [br_copycap]
    jae .endraw
    mov ax, [br_ctxpx]
    mov es, ax
    mov al, [es:si]
    inc si
    cmp al, 13
    je .newline
    cmp al, 10
    je .newline
    cmp al, 9
    je .space
    cmp al, 32
    jb .replace
    cmp al, 126
    ja .replace
    jmp .emit
.space:
    mov al, ' '
    jmp .emit
.replace:
    mov al, '?'
.emit:
    mov dx, [br_ctxpy]
    mov es, dx
    stosb
    jmp .raw
.newline:
    cmp al, 13
    jne .line
    mov ax, [br_ctxpx]
    mov es, ax
    cmp byte [es:si], 10
    jne .line
    inc si
.line:
    mov al, D_BR
    mov dx, [br_ctxpy]
    mov es, dx
    stosb
    jmp .raw
.endraw:
    mov al, D_END
    mov dx, [br_ctxpy]
    mov es, dx
    stosb
    mov [br_doclen], di
    jmp .layout
.rendered:
    mov byte [br_viewsrc], 0
    call br_parse
.layout:
    mov word [br_cols], 0
    mov word [br_top], 0
    mov word [br_ptop], 0
    mov si, [br_win]
    call br_measure
    call br_paint_band
    call br_sbar
    call br_settitle
    jmp .out
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

; Source pages contain the page's title already, so the Browser keeps the same
; IE-style suffix while changing only the mode prefix.
br_settitle:
    push ax
    push bx
    push cx
    push di
    push si
    push es
    mov bx, [br_win]
    cmp byte [br_ptitle], 0
    je .default
    push ds
    pop es
    mov si, br_ptitle
    mov di, br_wtitle
    mov cx, BR_TITMAX - 1
.copy:
    lodsb
    or al, al
    jz .suffix
    stosb
    loop .copy
.suffix:
    mov si, br_s_titlesfx
    mov cx, br_s_titlesfx_end - br_s_titlesfx
    rep movsb
    mov ax, br_wtitle
    jmp .title
.default:
    mov ax, br_s_webbrowser
.title:
    call OSAPI_WM_TITLE
    pop es
    pop si
    pop di
    pop cx
    pop bx
    pop ax
    ret

; The toolbar star reserves its own 16px hit target and draws one 8x8 bitmap.
%include "os88data.inc"
