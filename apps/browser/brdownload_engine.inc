br_dlx_link:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    push es
    cmp byte [br_dl_state], DL_OPEN
    jb .available
    cmp byte [br_dl_state], DL_DONE
    jae .available
    ; A close can remove the manager window just before its final WM_WAKE is
    ; delivered. If the worker has already reached CANCELREADY, reap that
    ; acknowledged cancellation here before deciding whether a new link is
    ; still busy; the UI callback owns the file cleanup and partial deletion.
    cmp byte [br_dl_cancel_req], 0
    je .busymsg
    cmp byte [br_dl_state], DL_CANCELREADY
    jne .cancelling
    cmp word [br_dl_win], 0
    jne .cancelling
    call br_dlx_onwake
    cmp byte [br_dl_state], DL_DONE
    jae .available
    cmp byte [br_dl_state], DL_OPEN
    jb .available
    jmp short .busymsg
.cancelling:
    mov si, br_dl_s_busy
    mov si, br_dl_s_cancelling
.busymsg:
    cmp byte [br_dl_cancel_req], 0
    jne .report
    mov si, br_dl_s_busy
.report:
    call far [br_dl_r_errst]
    jmp .out
.available:
    ; Preserve the caller's link index before AL is reused for the previous
    ; manager state. On a second download, a terminal state is nonzero and
    ; would otherwise corrupt the low byte of the link index.
    mov [br_dl_index], ax
    mov al, [br_dl_state]
    mov [br_dl_prevstate], al
    mov si, br_ubuf
    mov di, br_dl_barbak
    mov cx, BR_UBUF
    call far [br_dl_r_copyz]
    mov si, br_host
    mov di, br_dl_hostbak
    mov cx, BR_HOSTMAX
    call far [br_dl_r_copyz]
    mov si, br_path
    mov di, br_dl_pathbak
    mov cx, BR_PATHMAX
    call far [br_dl_r_copyz]
    mov ax, [br_port]
    mov [br_dl_portbak], ax

    cmp word [br_dl_index], 0FFFFh ; automatic non-page navigation
    je .direct
    cmp word [br_dl_index], 0FFFEh ; Save Image As: the image's src, against
    jne .lnk                       ; the page, as part 1 fetches it (71.32)
    mov bx, [br_ctximg]
    mov cl, 4
    shl bx, cl                     ; BR_ITSZ = 16
    mov si, [br_itab + bx + 2]     ; IT_SRC (brimage_const.inc)
    cmp si, 0FFFFh                 ; no src kept (a frame child's, src="",
    je .bad                        ; Links limited): no URL to save
    cmp word [br_lnkseg], 0
    je .bad
    mov es, [br_lnkseg]
    jmp short .src
.lnk:
    mov bx, [br_dl_index]
    mov dl, [br_lnkfrm+bx]
    test dl, dl
    jz .href
    dec dl
    xor bx, bx
    mov bl, dl
    shl bx, 1
    mov si, [br_framebaseoff+bx]
    cmp si, 0FFFFh
    je .bad
    mov es, [br_lnkseg]
    mov di, br_ubuf
.base:
    mov al, [es:si]
    inc si
    mov [di], al                    ; ES is the link arena: no stosb here
    inc di
    test al, al
    jnz .base
    mov si, br_ubuf
    call far [br_dl_r_split]
    jc .bad
.href:
    mov bx, [br_dl_index]
    shl bx, 1
    mov si, [br_lnkoff+bx]
    mov es, [br_lnkseg]
.src:
    ; An absolute URL needs no page/frame base. Copy it from the link arena
    ; directly before the generic resolver rewrites the Browser's address
    ; buffer; relative hrefs still use the normal frame-aware resolver.
    mov bx, br_s_scheme
    call far [br_dl_r_hrefpfx]
    jnc .absolute
    call far [br_dl_r_resolve]
    jc .restore
    mov si, br_ubuf
    mov di, br_dl_url
    mov cx, BR_DL_URLMAX
    push ds
    pop es
    call far [br_dl_r_copyz]
    jmp .resolved
.absolute:
    mov di, br_dl_url
    mov cx, BR_DL_URLMAX-1
.absloop:
    mov al, [es:si]
    inc si
    or al, al
    jz .absterm
    jcxz .badrestore
    mov [di], al
    inc di
    loop .absloop
    cmp byte [es:si], 0
    jne .badrestore
.absterm:
    mov byte [di], 0
    push ds
    pop es
    jmp .resolved
.direct:
    mov si, br_ubuf
    mov di, br_dl_url
    mov cx, BR_DL_URLMAX
    call far [br_dl_r_copyz]
.resolved:
    cmp byte [br_dl_url], 'h'
    jne .badrestore
    mov si, br_dl_url
    mov bx, br_s_scheme
    call far [br_dl_r_prefix]
    jc .badrestore
    call br_dl_split
    jc .badrestore
    call br_dl_defaultname
    mov byte [br_dl_state], DL_PICK
    mov al, FDLG_SAVE
    mov bx, [br_win]
    mov di, br_dl_saved
    mov si, br_dl_name
    call OSAPI_FILE_DLG
    jc .pickrefused
    jmp .restore
.pickrefused:
    mov al, [br_dl_prevstate]
    mov [br_dl_state], al
    mov si, br_dl_s_nodisk
    call far [br_dl_r_errst]
    jmp .restore
.badrestore:
    push ds
    pop es
    mov si, br_dl_s_url
    call far [br_dl_r_errst]
    jmp .restore
.bad:
    mov si, br_dl_s_url
    call far [br_dl_r_errst]
.restore:
    mov si, br_dl_barbak
    mov di, br_ubuf
    mov cx, BR_UBUF
    call far [br_dl_r_copyz]
    mov si, br_dl_hostbak
    mov di, br_host
    mov cx, BR_HOSTMAX
    call far [br_dl_r_copyz]
    mov si, br_dl_pathbak
    mov di, br_path
    mov cx, BR_PATHMAX
    call far [br_dl_r_copyz]
    mov ax, [br_dl_portbak]
    mov [br_port], ax
.out:
    pop es
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Bounded NUL copy, caller's CX is destination capacity including NUL.
br_dl_defaultname:
    push ax
    push bx
    push cx
    push si
    push di
    cmp word [br_dl_index], 0FFFFh  ; a page fetch that became a download
    jne .url                        ; (SPEC.md 71.20.4) suggests the server's
    cmp byte [br_cdstate], 0FEh     ; filename= when it gave a usable one; a
    jne .url                        ; .zip link's br_go cleared it, and a link
    cmp byte [br_cdlen], 0          ; index never reads it
    je .url
    mov si, br_cdname
    jmp short .named                ; ...the copy loop below, from SI
.url:
    mov si, br_dl_path
    mov bx, si
.scan:
    mov al, [si]
    or al, al
    jz .end
    cmp al, '/'
    jne .query
    lea bx, [si+1]
    jmp .next
.query:
    cmp al, '?'
    je .end
    cmp al, '#'
    je .end
.next:
    inc si
    jmp .scan
.end:
    mov cx, si
    sub cx, bx
    or cx, cx
    jz .fallback
    cmp cx, 12
    ja .fallback
    mov si, bx
    mov di, br_dl_name
    rep movsb
    mov byte [di], 0
    jmp .out
.fallback:
    mov si, br_dl_s_default
.named:
    mov di, br_dl_name
    mov cx, 13
.copy:
    lodsb
    stosb
    or al, al
    jnz .copy
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

br_dlx_saved:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov [br_win], si
    cmp byte [es:di], 0
    jne .chosen
    mov al, [br_dl_prevstate]
    mov [br_dl_state], al
    jmp .out
.chosen:
    mov si, di
    mov di, br_dl_name
    mov cx, 13
.name:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    or al, al
    jnz .name
    call br_dl_openwin
    cmp word [br_dl_win], 0
    je .uiunavailable
    mov byte [br_dl_state], DL_IDLE
    mov byte [br_dl_fail_reason], 0
    mov byte [br_dl_have_len], 0
    mov byte [br_dl_headers_done], 0
    mov byte [br_dl_transfer_encoding], 0
    mov byte [br_dl_status_seen], 0
    mov byte [br_dl_redirs], 0
    mov byte [br_dl_cancel_req], 0
    mov byte [br_dl_close_after], 0
    mov byte [br_dl_mgr_close_after], 0
    mov byte [br_dl_pending], 0
    mov byte [br_dl_hnd], 0
    mov byte [br_dl_stream], 0
    mov byte [br_dl_seq], 0
    mov word [br_dl_linelen], 0
    mov word [br_dl_token], 0
    mov word [br_dl_fill], 0
    mov word [br_dl_recvbytes], 0
    mov word [br_dl_recvbytes+2], 0
    mov byte [br_dl_recvseq], 0
    mov word [br_dl_written], 0
    mov word [br_dl_written+2], 0
    mov word [br_dl_total], 0
    mov word [br_dl_total+2], 0
    mov word [br_dl_msg], 0
    mov word [br_dl_lastmsg], br_dl_s_connect
    call br_dl_destination
    jc .disk
    call br_dl_tmpmake              ; the name the bytes go under until done
    jc .disk
    call br_dl_split
    jc .url
    call br_dl_claim
    jc .memory
    call br_dl_create_file
    jc .disk
    mov byte [br_dl_state], DL_OPEN
    mov word [br_dl_ticks], 0
    call OSAPI_GET_TICKS
    mov [br_dl_starttick], ax
    mov word [br_dl_speed], 0
    mov byte [br_dl_anim], 0
    call br_dl_timerarm
    call br_dl_repaint_locked
    jmp .out
.memory:
    mov si, br_dl_s_nomem
    jmp .fail
.uiunavailable:
    mov si, br_dl_s_http
    jmp .fail
.url:
    mov si, br_dl_s_url
    jmp .fail
.disk:
    mov si, br_dl_s_nodisk
.fail:
    mov [br_dl_msg], si
    call br_dl_freebuf
    mov byte [br_dl_state], DL_FAIL
    call br_dl_repaint_locked
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

br_dl_destination:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call OSAPI_FILE_HERE
    mov [br_dl_dir], dx
    mov [br_dl_drive], bl
    push ds
    pop es
    mov di, br_dl_dest+2
    mov cx, BR_DL_DEST-16
    call OSAPI_FILE_PATH
    jc .bad
    ; FILE_PATH returns CX bytes (without NUL) at the current dialog folder.
    mov di, br_dl_dest+2
    add di, cx
    cmp byte [di-1], 0x5C
    je .leaf
    mov byte [di], 0x5C
    inc di
.leaf:
    mov si, br_dl_name
    mov cx, 13
    rep movsb
    ; br_dl_dest+2 already has room for the drive prefix. Keep the folder and
    ; leaf in place; shifting them again duplicates the first two characters
    ; in the displayed destination (for example B:\M\mem196k.htm at root).
    mov al, [br_dl_drive]
    add al, 'A'
    mov [br_dl_dest], al
    mov byte [br_dl_dest+1], ':'
    clc
    jmp .out
.bad:
    cld
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

br_dl_split:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es                         ; ES = OURS for the stosb stores below:
    push ds                         ; br_dl_saved is a FILE_DLG callback, and
    pop es                          ; ES = KERNEL_SEG on entry to every callback
                                    ; (SPEC.md 20) - so these stores used to land
                                    ; at KERNEL_SEG:br_dl_host, inside the
                                    ; kernel's .vga drawing code
    mov si, br_dl_url
    mov bx, br_s_scheme
    call far [br_dl_r_prefix]
    jc .bad
    add si, 7
    mov di, br_dl_host
    mov cx, BR_DL_HOSTMAX-1
    mov word [br_dl_port], 80
.host:
    mov al, [si]
    cmp al, ':'
    je .port
    cmp al, '/'
    je .path
    or al, al
    jz .slash
    or cx, cx
    jnz .hoststore
    jmp .bad
.hoststore:
    stosb
    inc si
    loop .host
    jmp .bad
.port:
    mov byte [di], 0
    inc si
    xor ax, ax
    xor cx, cx
.pnum:
    mov bl, [si]
    or bl, bl
    jz .portdone
    cmp bl, '/'
    je .portdone
    cmp bl, '0'
    jb .bad
    cmp bl, '9'
    ja .bad
    sub bl, '0'
    xor bh, bh
    push bx
    mov bx, 10
    mul bx
    pop bx
    or dx, dx
    jnz .bad
    add ax, bx
    jc .bad
    inc cx
    inc si
    jmp .pnum
.portdone:
    or cx, cx
    jz .bad
    or ax, ax
    jz .bad
    cmp byte [si], '/'
    je .portpath
    cmp byte [si], 0
    jne .bad
    mov [br_dl_port], ax
    mov si, br_dl_rootpath
    mov di, br_dl_path
    mov cx, BR_DL_PATHMAX
    call far [br_dl_r_copyz]
    jmp .ok
.portpath:
    mov [br_dl_port], ax
    jmp .path
.slash:
    mov byte [di], 0
    mov si, br_dl_rootpath
    mov di, br_dl_path
    mov cx, BR_DL_PATHMAX
    call far [br_dl_r_copyz]
    jmp .ok
.path:
    mov byte [di], 0
    mov di, br_dl_path
    mov cx, BR_DL_PATHMAX-1
.pcopy:
    mov al, [si]
    or al, al
    jz .pdone
    cmp al, '?'
    jne .pstore
    jmp .pstore
.pstore:
    or cx, cx
    jnz .locstore
    jmp .bad
.locstore:
    stosb
    inc si
    loop .pcopy
    cmp byte [si], 0
    jne .bad
.pdone:
    mov byte [di], 0
.ok:
    cmp byte [br_dl_host], 0
    je .bad
    clc
    jmp .out
.bad:
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

br_dl_claim:
    push ax
    push bx
    push cx
    push dx
    push si
    call OSAPI_FILE_DFREE
    jc .bad
    or bx, bx
    jz .bad
    mov ax, bx
    mov bx, 512
    mul bx
    or dx, dx
    jnz .bad
    mov [br_dl_clustersz], ax
    add ax, 1023
    mov cx, 10
    shr ax, cl
    or ax, ax
    jnz .want
    inc ax
.want:
    mov [br_dl_bufkb], ax
    mov [br_dl_needkb], ax
    mov ax, [br_membase]
    or ax, ax
    jnz .base
    call far [br_dl_r_avail]        ; (br_avail: the pool too on kern_mega)
    mov [br_dl_membase], bx
    jmp .room
.base:
    mov [br_dl_membase], ax
.room:
    mov ax, [br_dl_needkb]
    call br_dl_memroom
    jc .bad
    mov ax, [br_dl_clustersz]       ; the buffer is the cluster ITSELF, carved
    call far [br_dl_r_urlgrow]      ; after the URL store (SPEC.md 71.23) -
    jc .bad                         ; not a claim of its own, so a transfer
    mov ax, [br_dl_clustersz]       ; costs no slot of MEM_OWNER_MAX. The
    mov [br_dl_bufsz], ax           ; reserve check above stays in KB, which
    clc                             ; only rounds it the cautious way. It
    jmp .out                        ; MOVES with the store: br_reloc names
                                    ; [br_urlseg], and the file writes pin it
                                    ; for the call
.bad:
    stc
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

br_dl_memroom:
    push ax
    push bx
    push cx
    push dx
    mov [br_dl_needkb], ax
    call far [br_dl_r_avail]        ; (br_avail: the pool too on kern_mega)
    mov [br_dl_largest], ax
    mov ax, [br_dl_membase]
    mov cx, 5
    xor dx, dx
    div cx
    or dx, dx
    jz .reserve
    inc ax
.reserve:
    add ax, [br_dl_needkb]
    cmp bx, ax
    jb .no
    mov ax, [br_dl_largest]
    cmp ax, [br_dl_needkb]
    jb .no
.yes:
    clc
    jmp .out
.no:
    stc
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; br_dl_tmpmake - the TEMPORARY 8.3 name a transfer is written under, in the
; destination folder (current here: br_dl_destination has just read it).
; The chosen base with the extension $$$, or $$1..$$9 when that one exists or
; IS the chosen name. A zero-capacity OSAPI_FILE_READ is the existence probe:
; it answers from the directory entry and reads no data (FERR_NOENT = free).
; out: CF=0 and br_dl_tmpname set; CF=1 when no free name or the disk failed
; -----------------------------------------------------------------------------
br_dl_tmpmake:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    mov si, br_dl_name
    mov di, br_dl_tmpname
    mov cx, 8
.base:
    lodsb
    or al, al
    jz .ext
    cmp al, '.'
    je .ext
    stosb
    loop .base
.ext:
    mov ax, '.$'
    stosw
    mov ax, '$$'
    stosw
    mov byte [di], 0
    mov bl, '1'                     ; the next last character to try
.probe:
    mov si, br_dl_tmpname           ; the chosen name itself is never ours
    mov dx, br_dl_name              ; to write: compare before probing
.same:
    lodsb
    xchg si, dx
    cmp al, [si]
    xchg si, dx
    jne .ask
    inc dx
    or al, al
    jnz .same
    jmp short .taken
.ask:
    push bx
    mov si, br_dl_tmpname
    xor bx, bx                      ; ES:BX is never written at capacity 0
    xor cx, cx
    xor dx, dx
    call OSAPI_FILE_READ
    pop bx
    jnc .taken                      ; an empty file of that name
    cmp ax, FERR_NOENT
    je .free
    cmp ax, FERR_BIG
    jne .bad                        ; the disk, not the name
.taken:
    cmp bl, '9'
    ja .bad
    mov [di-1], bl
    inc bl
    jmp short .probe
.free:
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
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; br_dl_commit - put a completed transfer under the chosen name, in the
; destination folder: delete an existing file of that name (FERR_NOENT is no
; error), then rename the temporary file onto it (OSAPI_FILE_RENAME refuses
; an existing name, hence the delete first). CF=1 leaves the temporary file
; where it is: from the delete on, it is the only copy of anything.
; -----------------------------------------------------------------------------
br_dl_commit:
    push bx
    push dx
    push si
    push di
    call OSAPI_FILE_HERE
    mov [br_dl_dirbak], dx
    mov [br_dl_drivebak], bl
    mov dx, [br_dl_dir]
    mov bl, [br_dl_drive]
    call OSAPI_FILE_GOTO
    jc .restore
    mov si, br_dl_name
    call OSAPI_FILE_DELETE
    jnc .rename
    cmp ax, FERR_NOENT
    stc
    jne .restore
.rename:
    mov si, br_dl_tmpname
    mov di, br_dl_name
    call OSAPI_FILE_RENAME
.restore:
    pushf
    push ax
    mov dx, [br_dl_dirbak]
    mov bl, [br_dl_drivebak]
    call OSAPI_FILE_GOTO
    pop ax
    popf
    pop di
    pop si
    pop dx
    pop bx
    ret

br_dl_create_file:
    push ax
    push bx
    push cx
    push dx
    push es
    push ds
    pop es
    mov si, br_dl_tmpname           ; never the chosen name: an existing file
    xor bx, bx                      ; of that name survives until the
    xor cx, cx                      ; transfer completes (br_dl_commit)
    xor dx, dx
    call OSAPI_FILE_WRITE
    pop es
    pop dx
    pop cx
    pop bx
    pop ax
    ret

br_dl_openwin:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, [br_dl_win]
    or bx, bx
    jnz .front
    call OSAPI_VIDEO
    mov [br_dl_screenh], bx
    cmp bx, 460
    jae .tall
    cmp bx, 320
    jae .medium
.compact:
    mov byte [br_dl_layout], 0
    mov word [br_dl_tpl+6], 164
    mov word [br_dl_yicon], 0
    mov word [br_dl_ycaption], 49
    mov word [br_dl_ypath], 210
    mov word [br_dl_yprogress], 61
    mov word [br_dl_barh], 10
    mov word [br_dl_ydetail], 74
    mov word [br_dl_detailstep], 8
    mov word [br_dl_ystats], 112
    mov word [br_dl_yspeed], 120
    mov word [br_dl_ybutton], 130
    jmp .widthfit
.medium:
    mov byte [br_dl_layout], 1
    mov word [br_dl_tpl+6], 228
    mov word [br_dl_yicon], 10
    mov word [br_dl_ycaption], 66
    mov word [br_dl_ypath], 285
    mov word [br_dl_yprogress], 78
    mov word [br_dl_barh], 14
    mov word [br_dl_ydetail], 100
    mov word [br_dl_detailstep], 13
    mov word [br_dl_ystats], 152
    mov word [br_dl_yspeed], 168
    mov word [br_dl_ybutton], 168
    jmp .widthfit
.tall:
    mov byte [br_dl_layout], 2
    mov word [br_dl_tpl+6], 204
    mov word [br_dl_yicon], 5
    mov word [br_dl_ycaption], 66
    mov word [br_dl_ypath], 285
    mov word [br_dl_yprogress], 78
    mov word [br_dl_barh], 12
    mov word [br_dl_ydetail], 100
    mov word [br_dl_detailstep], 12
    mov word [br_dl_ystats], 148
    mov word [br_dl_yspeed], 164
    mov word [br_dl_ybutton], 164
    mov [br_dl_w], ax                ; retain screen width before WM_GEOM
    cmp ax, 456
    jb .tallnarrow
    mov word [br_dl_tpl+4], 440
    sub ax, 440
    shr ax, 1
    mov [br_dl_tpl], ax
    jmp .fitdone
.tallnarrow:
    sub ax, 16
    mov [br_dl_tpl+4], ax
    mov word [br_dl_tpl], 8
    jmp .fitdone
.widthfit:
    cmp ax, 456
    jb .narrow
    mov word [br_dl_tpl+4], 440
    sub ax, 440
    shr ax, 1
    mov [br_dl_tpl], ax
    jmp .fitdone
.narrow:
    sub ax, 16
    mov [br_dl_tpl+4], ax
    mov word [br_dl_tpl], 8
.fitdone:
    ; Center in the usable desktop below the menu bar on VGA, Hercules and
    ; CGA; OSAPI_VIDEO returns the primary height in BX.
    mov ax, [br_dl_screenh]
    sub ax, [br_dl_tpl+6]
    sub ax, 20
    shr ax, 1
    add ax, 20
    mov [br_dl_tpl+2], ax
    mov si, br_dl_tpl
    call OSAPI_WM_CREATE
    jc .failed
    mov [br_dl_win], bx
    mov ax, bx                      ; the handlers are br_dl_cls's (SPEC.md
    mov bx, br_dl_btrec             ; 11.104); btninit only links the record
    mov dx, br_dl_onclick
    call far [br_dl_r_uibtninit]
    mov bx, [br_dl_win]
    call OSAPI_WM_SHOW
    jmp .front
.failed:
    mov word [br_dl_win], 0
    jmp .out
.front:
    mov bx, [br_dl_win]
    call OSAPI_WM_SHOW
    call OSAPI_WM_FRONT
    call br_dl_repaint_locked
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Fetch and bind the optional painter on the UI task. The worker never enters
; this part; it remains in the primary segment with all OS callbacks.
br_dl_timerarm:
    push ax
    push bx
    mov bx, [br_dl_win]
    or bx, bx
    jz .out
    mov ax, 5
    call OSAPI_WM_TIMER
.out:
    pop bx
    pop ax
    ret

br_dl_redraw:
    push ax
    push bx
    push si
    mov bx, [br_dl_win]
    or bx, bx
    jz .out
    call OSAPI_GFX_LOCK
    mov bx, [br_dl_win]
    call OSAPI_WM_CLIP_SET
    jc .unlock
    mov si, [br_dl_win]
    call br_dlx_paint
.unlock:
    call OSAPI_GFX_UNLOCK
.out:
    pop si
    pop bx
    pop ax
    ret

; Entry, menu, and FILE_DLG callbacks already hold the UI graphics lock.
br_dl_repaint_locked:
    push bx
    push si
    mov bx, [br_dl_win]
    or bx, bx
    jz .out
    call OSAPI_WM_CLIP_SET
    jc .out
    mov si, [br_dl_win]
    call br_dlx_paint
.out:
    pop si
    pop bx
    ret

; The manager's whole face: the painter (brdownload_paint.inc) and the button.
; SI = the window, as W_PAINT hands it
br_dlx_paint:
    call br_dl_paint
    call br_dl_button
    ret

br_dl_button:
    push ax
    push bx
    push cx
    push dx
    mov word [br_dl_btncount], 1
    mov word [br_dl_btrecn], 1
    mov ax, [br_dl_x]
    add ax, [br_dl_w]
    sub ax, 64
    mov [br_dl_btnrects], ax
    mov bx, [br_dl_y]
    add bx, [br_dl_ybutton]
    mov [br_dl_btnrects+2], bx
    add ax, 56
    mov [br_dl_btnrects+4], ax
    add bx, 12
    mov [br_dl_btnrects+6], bx
    mov word [br_dl_btnlabels], br_dl_s_exit
    mov bl, [br_dl_state]
    cmp bl, DL_OPEN
    jb .draw
    cmp bl, DL_ENDWAIT
    jb .cancel
    cmp bl, DL_DONE
    jae .draw
    ; Network transfer is over, but the last staged disk write is still
    ; pending. Do not offer Exit until the file is fully committed.
    mov word [br_dl_btncount], 0
    mov word [br_dl_btrecn], 0
    jmp short .draw
.cancel:
    ; While active, offer only cancellation. The terminal state replaces it
    ; with Exit, which destroys this secondary window and frees its slot.
    mov ax, [br_dl_x]
    add ax, [br_dl_w]
    sub ax, 64
    mov [br_dl_btnrects], ax
    add ax, 56
    mov [br_dl_btnrects+4], ax
    mov word [br_dl_btnlabels], br_dl_s_cancel
.draw:
    mov bx, br_dl_btrec
    mov al, 1
    call far [br_dl_r_uibtn]
    pop dx
    pop cx
    pop bx
    pop ax
    ret

br_dlx_timer:
    push ax
    push bx
    push cx
    push dx
    push si
    mov al, [br_dl_state]
    cmp al, DL_OPEN
    jb .static
    cmp al, DL_ENDWAIT
    jae .static
    inc byte [br_dl_anim]
    and byte [br_dl_anim], 7
    call br_dl_speedupdate
    mov bx, [br_dl_win]
    call OSAPI_WM_CLIP_SET
    jc .hidden
    call br_dl_redraw_doc
    call br_dl_paint_live
.arm:
    mov bx, [br_dl_win]
    mov ax, 5
    call OSAPI_WM_TIMER
    jmp .out
.hidden:
    ; Keep state and timing current while covered; the next expose paints the
    ; current frame through the ordinary W_PAINT callback.
    mov bx, [br_dl_win]
    mov ax, 5
    call OSAPI_WM_TIMER
    jmp .out
.static:
    cmp al, DL_DONE
    jne .paint
    mov byte [br_dl_anim], 7
.paint:
    call br_dl_speedupdate
    mov bx, [br_dl_win]
    call OSAPI_WM_CLIP_SET
    jc .out
    mov si, [br_dl_win]
    call br_dlx_paint
    mov bx, [br_dl_win]
    cmp byte [br_dl_state], DL_OPEN
    jb .out
    cmp byte [br_dl_state], DL_ENDWAIT
    jae .out
    mov ax, 18
    call OSAPI_WM_TIMER
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Frequent updates are confined to the bar, status row and transfer counters.
; The longer URL/name/destination rows remain static during a transfer.
br_dl_speedupdate:
    push ax
    push bx
    push cx
    push dx
    push si
    call OSAPI_GET_TICKS
    sub ax, [br_dl_starttick]
    jnz .elapsed
    cmp byte [br_dl_state], DL_DONE
    jne .out
    ; A quick transfer may finish before one 18.2 Hz tick elapses. Use one
    ; tick as the smallest measurable interval so the final display settles.
    inc ax
.elapsed:
    mov [br_dl_ticks], ax
    mov ax, [br_dl_recvbytes]
    mov dx, [br_dl_recvbytes+2]
    ; Average KiB/s over the whole transfer: timer-sized deltas lose small
    ; chunks on slow links and can leave a fast completed transfer unmeasured.
    mov cx, 10
.shift:
    shr dx, 1
    rcr ax, 1
    loop .shift
    or dx, dx
    jnz .cap
    or ax, ax
    jnz .scale
    cmp word [br_dl_recvbytes], 0
    jne .minimum
    cmp word [br_dl_recvbytes+2], 0
    jne .minimum
    jmp .clear
.minimum:
    mov ax, 1
.scale:
    mov cx, 18
    mul cx
    mov cx, [br_dl_ticks]
    cmp dx, cx
    jae .cap                       ; quotient would exceed 16 bits
    div cx
    or ax, ax
    jnz .store
    mov ax, 1
.store:
    mov [br_dl_speed], ax
    jmp .out
.cap:
    mov word [br_dl_speed], 999
    jmp .out
.clear:
    mov word [br_dl_speed], 0
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

br_dlx_onup:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, br_dl_btrec
    call far [br_dl_r_uibtnup]
    or al, al
    jz .out
    mov al, [br_dl_state]
    ; ENDWAIT is still in-flight: the UI has not yet committed the final
    ; staged bytes or classified the result. Keep cancellation asynchronous
    ; through ENDWAIT/CANCELREQ/CANCELREADY; only a terminal result may close
    ; directly from the button callback.
    ; Every state from DL_DONE up is terminal - DONE, FAIL and CANCEL alike -
    ; and Exit on one only dismisses. FAIL used to fall through to a cancel
    ; request: the partial it deleted was already gone (or, under "Saved as
    ; .$$$", was the ONLY copy), so the window stayed on "Could not delete
    ; partial file" and could not be closed (tests/brdownload.py --fail-exit)
    cmp al, DL_OPEN
    jb .hide
    cmp al, DL_DONE
    jae .hide                      ; preserve a completed or kept file
.request_cancel:
    mov byte [br_dl_cancel_req], 1
    mov byte [br_dl_state], DL_CANCELREQ
    mov word [br_dl_msg], br_dl_s_canceling
    ; Match the title-bar close path: the worker finishes its current receive,
    ; the UI callback removes the partial file, then destroys this window.
    ; Do not repaint the full manager here; that made Cancel visibly flicker.
    mov byte [br_dl_mgr_close_after], 1
    mov bx, [br_dl_win]
    call OSAPI_WM_WAKE
    jmp .out
.hide:
    mov bx, [br_dl_win]
    mov word [br_dl_win], 0
    call OSAPI_WM_DESTROY
    ; (part 0 is dropped by the resident thunk on return: br_dl_after)
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

br_dlx_close:
    ; While active, closing is cancellation; completion destroys the manager
    ; asynchronously so the close negotiator never frees its window inline.
    mov al, [br_dl_state]
    cmp al, DL_OPEN
    jb .close_now
    cmp al, DL_DONE
    jae .close_now
    cmp al, DL_ENDWAIT
    jae .pending
    mov byte [br_dl_cancel_req], 1
    mov byte [br_dl_state], DL_CANCELREQ
    mov word [br_dl_msg], br_dl_s_canceling
    mov byte [br_dl_mgr_close_after], 1
    call br_dl_repaint_locked
    stc
    ret
.close_now:
    ; 2, not 1: nothing is in flight, so the wake only DISMISSES. 1 asks the
    ; wake to finish a cancellation, and on DL_FAIL that is the raced-cancel
    ; path, which keeps the error on the glass - so the close box did nothing
    ; on a failed download (tests/brdownload.py --fail-close)
    mov byte [br_dl_mgr_close_after], 2
    mov bx, [br_dl_win]
    call OSAPI_WM_WAKE
    stc
    ret
.pending:
    ; The transfer or its last disk write is completing. Wait for its terminal
    ; wake, then destroy this window instead of leaving a hidden download.
    mov byte [br_dl_mgr_close_after], 1
    stc
    ret
br_dlx_ondrag:
    mov bx, br_dl_btrec
    call far [br_dl_r_uibtndrag]
    ret

br_dlx_onwake:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es                         ; .write aims ES at the transfer buffer,
                                    ; and br_dlx_link calls this, then copies
                                    ; with stosb (roadmap section 3)
    cmp byte [br_dl_mgr_close_after], 0
    je .checkpending
    cmp byte [br_dl_mgr_close_after], 2
    je .destroy_manager             ; a close on a settled manager
    mov al, [br_dl_state]
    cmp al, DL_OPEN
    jb .destroy_manager
    cmp al, DL_DONE
    je .destroy_manager             ; completed files must be preserved
    cmp al, DL_CANCEL
    je .destroy_manager             ; partial was already deleted
    cmp al, DL_FAIL
    je .cancel_terminal_fail        ; a user cancel raced a network failure
.checkpending:
    ; A click may change the visible state while a committed cluster is still
    ; pending. The cluster remains owned by the wake handler and must be
    ; acknowledged before the worker can observe the cancellation request.
    cmp byte [br_dl_pending], 0
    jne .write
    cmp byte [br_dl_state], DL_CANCELREADY
    je .cancelled
    cmp byte [br_dl_state], DL_PENDING
    je .write
    cmp byte [br_dl_state], DL_ENDWAIT
    je .end
    jmp .out
.destroy_manager:
    mov byte [br_dl_mgr_close_after], 0
    mov bx, [br_dl_win]
    mov word [br_dl_win], 0
    or bx, bx
    jz .drop_part
    call OSAPI_WM_DESTROY
.drop_part:
    ; (part 0 is dropped by the resident thunk on return: br_dl_after)
    jmp .out
.cancel_terminal_fail:
    ; The worker may report a socket failure in the same tick as Cancel.
    ; Treat the explicit user request as cancellation and remove any partial
    ; before closing; if deletion itself fails, keep the manager to show it.
    cmp byte [br_dl_cancel_req], 0
    je .keep_terminal_error
    call br_dl_close_stream
    call br_dl_freebuf
    call br_dl_partial_delete
    jc .delete_error
    mov byte [br_dl_state], DL_CANCEL
    mov word [br_dl_msg], br_dl_s_cancelled
    jmp .destroy_manager
.delete_error:
    mov byte [br_dl_mgr_close_after], 0
    mov word [br_dl_msg], br_dl_s_delerror
    call br_dl_redraw
    jmp .out
.keep_terminal_error:
    mov byte [br_dl_mgr_close_after], 0
    call br_dl_redraw
    jmp .out
.write:
    ; --- ONE BAR FOR THE DOWNLOAD (SPEC.md 12.8.10, 71.20): a cluster is
    ; written a wake, so on a hard disk the widget blinked per cluster.
    ; Begun at the FIRST write, the one moment Content-Length is known on
    ; this task and nothing is on the disk yet; no length, no bar. The
    ; answer is not kept: br_dl_close_stream, which every way a download
    ; stops goes through, ends it unconditionally (an end not ours is
    ; refused), and a close of the Browser ends it in the kernel
    mov ax, [br_dl_written]
    or ax, [br_dl_written+2]
    jnz .wr
    cmp byte [br_dl_have_len], 0
    je .wr
    mov cx, [br_dl_total]
    mov dx, [br_dl_total+2]
    call OSAPI_FS_OP
.wr:
    mov si, br_dl_tmpname
    mov es, [br_urlseg]             ; the buffer is carved in the URL store
    mov bx, BR_URLDL                ; (SPEC.md 71.23), 512-aligned there
    mov cx, [br_dl_fill]
    cmp byte [br_dl_seq], 2
    je .append
    xor ax, ax                      ; ES:BX is the buffer and the kernel may
    call far [br_dl_r_pagepin]                 ; claim before its transfer pin applies
    mov di, [br_dl_token]           ; (SPEC.md 66.3 rule 5): pinned for the
    call OSAPI_FILE_WRITE_SEQ       ; call, AL = 0 being the verb as well
    call far [br_dl_r_pageunpin]               ; ...and free to move again, CF kept
    jnc .seqok
    cmp ax, FERR_NAME
    jne .writefail
    ; kern_small has no WRITE_SEQ slot. Its native APPEND path remains safe,
    ; cluster-bounded, and slower; keep the same staging protocol.
    mov byte [br_dl_seq], 2
.append:
    mov si, br_dl_tmpname
    mov es, [br_urlseg]             ; the buffer is carved in the URL store
    mov bx, BR_URLDL                ; (SPEC.md 71.23), 512-aligned there
    mov cx, [br_dl_fill]
    xor ax, ax
    call far [br_dl_r_pagepin]                 ; pinned for the call, as above
    mov dx, -1                      ; at the file's END (SPEC.md 18.4.7.3)
    call OSAPI_FILE_WRITE_AT
    call far [br_dl_r_pageunpin]
    jc .writefail
    jmp .wrote
.seqok:
    mov [br_dl_token], di
    mov byte [br_dl_seq], 1
    mov byte [br_dl_stream], 1
.wrote:
    mov ax, [br_dl_fill]
    add [br_dl_written], ax
    adc word [br_dl_written+2], 0
    mov word [br_dl_fill], 0
    mov byte [br_dl_pending], 0
    cmp byte [br_dl_final], 0
    jne .finishwrite
    mov byte [br_dl_state], DL_BODY
    jmp .out
.finishwrite:
    cmp byte [br_dl_final], 2
    je .cancelled
.finishdownload:
    mov byte [br_dl_state], DL_ENDWAIT
    jmp .end
.writefail:
    mov byte [br_dl_fail_reason], 1
    mov byte [br_dl_cancel_req], 1
    mov byte [br_dl_pending], 0
    mov word [br_dl_fill], 0
    mov byte [br_dl_state], DL_CANCELREQ
    mov word [br_dl_msg], br_dl_s_write
    jmp .out
.cancelled:
    call br_dl_close_stream
    call br_dl_freebuf
    call br_dl_partial_delete
    jnc .deleted
    mov byte [br_dl_fail_reason], 2
    mov word [br_dl_msg], br_dl_s_delerror
    jmp .failed
.deleted:
    ; Explicit Cancel / title-bar close means the partial is disposable. The
    ; stream finalizer can report an error while discarding its write handle;
    ; once deletion succeeded, that must not strand the manager in an error
    ; screen with a file the user asked us to remove.
    cmp byte [br_dl_mgr_close_after], 0
    jne .cancelled_ok
    cmp byte [br_dl_fail_reason], 0
    jne .failed
.cancelled_ok:
    mov byte [br_dl_state], DL_CANCEL
    mov word [br_dl_msg], br_dl_s_cancelled
    jmp .redraw
.failed:
    mov byte [br_dl_state], DL_FAIL
    jmp .redraw

.end:
    call br_dl_close_stream
    call br_dl_freebuf
    cmp byte [br_dl_fail_reason], 0
    jne .failedend
    call br_dl_commit               ; the temporary file takes the chosen name
    jc .commitfail
    mov byte [br_dl_state], DL_DONE
    mov byte [br_dl_anim], 7
    call br_dl_speedupdate
    mov word [br_dl_msg], br_dl_s_complete
    jmp .redraw
.commitfail:
    mov word [br_dl_msg], br_dl_s_kept ; complete, but under the $$$ name:
    mov byte [br_dl_cancel_req], 0  ; it may be the only copy, so no cancel
    jmp .failedstate                ; path may delete it
.failedend:
    cmp byte [br_dl_cancel_req], 0  ; a raced Cancel deletes it in
    jne .failedstate                ; .cancel_terminal_fail instead
    call br_dl_partial_delete       ; a failed transfer keeps no truncated file
.failedstate:
    mov byte [br_dl_state], DL_FAIL
    jmp .redraw
.redraw:
    call br_dl_redraw
    cmp byte [br_dl_mgr_close_after], 0
    je .mainclose
    ; Keep the window available to report a failed cleanup. A successfully
    ; cancelled transfer reaches DL_CANCEL only after the partial file has
    ; been deleted; terminal I/O errors remain visible for the user.
    cmp byte [br_dl_state], DL_CANCEL
    je .destroy_manager
    cmp byte [br_dl_state], DL_DONE
    je .destroy_done                ; Close during the final write: finished
    cmp byte [br_dl_state], DL_FAIL
    jne .mainclose
    jmp .cancel_terminal_fail
.destroy_done:
    mov byte [br_dl_mgr_close_after], 0
    mov bx, [br_dl_win]
    or bx, bx
    jz .mainclose
    mov word [br_dl_win], 0
    call OSAPI_WM_DESTROY
.mainclose:
    cmp byte [br_dl_close_after], 0
    je .out
    mov byte [br_dl_close_after], 0
    mov bx, [br_win]
    call OSAPI_WM_CLOSE
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; The save dialog's destination is br_dl_dir/br_dl_drive. Restore that exact
; instance folder around deletion so a later directory change cannot leave
; the partial file behind.
br_dl_partial_delete:
    push bx
    push dx
    push si
    call OSAPI_FILE_HERE
    mov [br_dl_dirbak], dx
    mov [br_dl_drivebak], bl
    mov dx, [br_dl_dir]
    mov bl, [br_dl_drive]
    call OSAPI_FILE_GOTO
    jc .restore
    mov si, br_dl_tmpname           ; ours alone: the chosen name is untouched
    call OSAPI_FILE_DELETE
    pushf
    push ax
    jmp short .restore_saved
.restore:
    pushf
    push ax
.restore_saved:
    mov dx, [br_dl_dirbak]
    mov bl, [br_dl_drivebak]
    call OSAPI_FILE_GOTO
    pop ax
    popf
    pop si
    pop dx
    pop bx
    ret

br_dl_close_stream:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    xor cx, cx                      ; the download's one bar comes down
    xor dx, dx                      ; (.write's OSAPI_FS_OP): every terminal
    call OSAPI_FS_OP                ; path comes through here
    cmp byte [br_dl_seq], 2
    je .fallback
    cmp byte [br_dl_stream], 0
    je .out
    mov si, br_dl_tmpname
    mov al, 0
    mov di, [br_dl_token]
    xor cx, cx
    call OSAPI_FILE_WRITE_SEQ
    jc .bad
    mov byte [br_dl_stream], 0
    mov word [br_dl_token], 0
    jmp .out
.bad:
    mov byte [br_dl_fail_reason], 1
    mov word [br_dl_msg], br_dl_s_write
    jmp .out
.fallback:
    mov byte [br_dl_seq], 0
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

; br_dlx_put - CX bytes at DS:SI onto the cluster buffer at BR_URLDL +
; [br_dl_fill], [br_dl_chunkn] = CX. Worker. A Copy's regrow can MOVE the URL
; store while this task is pre-empted, which a compaction never does (it waits
; for the worker to park), so [br_dl_wbusy] covers the load of [br_urlseg] and
; the copy, and a regrow already under way ([br_dl_ugrow]) is waited out: each
; side's byte is stored before it reads the other's, br_imghalt's handshake
; (SPEC.md 71.23). ES = the store; AX, CX, DI clobbered, SI past the bytes
br_dlx_put:
    mov [br_dl_chunkn], cx
.wait:
    mov byte [br_dl_wbusy], 1       ; FIRST, then the UI's byte
    cmp byte [br_dl_ugrow], 0
    je .copy
    mov byte [br_dl_wbusy], 0
    call OSAPI_TASK_YIELD
    jmp short .wait
.copy:
    mov ax, [br_dl_fill]
    mov es, [br_urlseg]
    mov di, BR_URLDL
    add di, ax
    cld
    rep movsb
    mov byte [br_dl_wbusy], 0
    ret

br_dl_freebuf:
    push ax
    push dx
    cmp word [br_dl_bufsz], 0       ; no buffer carved
    je .out
    xor ax, ax                      ; the URL store gives it back: a shrink,
    call far [br_dl_r_urlgrow]      ; which succeeds in place
    mov word [br_dl_bufsz], 0
.out:
    pop dx
    pop ax
    ret

; Main Browser close: synchronously close owned resources; the kernel then
; tears down the single worker and all instance claims together.
br_dlx_mainclose:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    ; A live worker owns the socket and can be preempted between any two
    ; instructions. Refuse this close, request cancellation, and let its
    ; acknowledged wake finish the file cleanup before WM_CLOSE retries it.
    mov al, [br_dl_state]
    cmp al, DL_OPEN
    jb .close_now
    cmp al, DL_DONE
    jae .close_now
    mov byte [br_dl_close_after], 1
    cmp al, DL_CANCELREQ
    je .cancel_waiting
    cmp al, DL_CANCELREADY
    je .cancel_waiting
    mov byte [br_dl_cancel_req], 1
    mov byte [br_dl_state], DL_CANCELREQ
    mov word [br_dl_msg], br_dl_s_canceling
.cancel_waiting:
    stc
    jmp .out
.close_now:
    ; The worker also owns the ordinary page-fetch socket. The generation
    ; handshake makes this unconditional abort safe even after a settled page.
    call far [br_dl_r_abort]
    mov al, [br_dl_hnd]
    or al, al
    jz .stream
    mov bh, NET_CLASS
    mov bl, NETV_CLOSE
    call OSAPI_DRV_CALL
    mov byte [br_dl_hnd], 0
.stream:
    call br_dl_close_stream
    call br_dl_freebuf
    mov bx, [br_dl_win]
    or bx, bx
    jz .done
    mov word [br_dl_win], 0
    call OSAPI_WM_DESTROY
.done:
    ; (part 0 is dropped by the resident thunk on return: br_dl_after)
    clc
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Download worker. It shares the Browser's one task but has its own URL,
; request, socket, parser and counters. br_rxb is reused only after each
; current receive has been consumed, and the disk cluster stays parked until
; the UI task acknowledges it.
;
; THE WORKER ENTERS THIS PART ONLY THROUGH brdownload.inc's br_dl_step, and
; only in DL_OPEN..DL_BODY with nothing pending and no cancel request. It
; never PUBLISHES a hand-over itself: where the transfer gives the UI a
; cluster (DL_PENDING) or its end (DL_ENDWAIT), this sets br_dl_publish and
; returns, and br_dl_step stores the state and wakes the UI back in the
; primary. The UI may drop part 0 only from a state it reaches after a
; hand-over, so this code is never running when its segment goes
; (SPEC.md 71.20.5)
; =============================================================================
br_dlx_step:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    mov al, [br_dl_state]
    cmp al, DL_OPEN
    je .open
    cmp al, DL_WAIT
    je .wait
    cmp al, DL_SEND
    je .send
    cmp al, DL_HEAD
    je .head
    cmp al, DL_BODY
    je .body
    jmp .out
.open:
    call far [br_dl_r_net_find]
    jc .nodriver
    jz .nolink
    mov bh, NET_CLASS
    mov bl, NETV_OPEN
    mov si, br_dl_host
    mov cx, [br_dl_port]
    call OSAPI_DRV_CALL
    jc .retry
    mov [br_dl_hnd], al
    ; This socket carries a file body, not a page or control exchange. Ask
    ; NETV_BULK for the shared large receive ring while the socket is still
    ; empty; refusal is intentionally harmless (the driver keeps the lean
    ; ring), matching SPEC.md 72.21.1.
    mov bh, NET_CLASS
    mov bl, NETV_BULK
    call OSAPI_DRV_CALL
    mov byte [br_dl_state], DL_WAIT
    jmp .out
.nodriver:
    mov word [br_dl_msg], br_dl_s_nodriver
    jmp .fail
.nolink:
    mov word [br_dl_msg], br_dl_s_nolink
    jmp .fail
.wait:
    call br_dl_qstat
    jc .retry
    cmp ah, NSK_UP
    je .connected
    cmp ah, NSK_CONNECT
    je .out
    mov word [br_dl_msg], br_dl_s_http
    jmp .fail
.connected:
    call br_dl_request
    mov byte [br_dl_state], DL_SEND
    jmp .out
.send:
    mov si, br_dl_reqb
    add si, [br_dl_sent]
    mov cx, [br_dl_reqn]
    sub cx, [br_dl_sent]
    mov al, [br_dl_hnd]
    mov bh, NET_CLASS
    mov bl, NETV_SEND
    call OSAPI_DRV_CALL
    jc .retry
    add [br_dl_sent], cx
    mov ax, [br_dl_sent]
    cmp ax, [br_dl_reqn]
    jb .out
    mov byte [br_dl_state], DL_HEAD
    jmp .out
.head:
    cmp byte [br_nstate], BN_SEND   ; br_rxb still holds the page's unsent request
    je .out
    mov cx, BR_CHUNK
    call br_dl_recv
    jc .retry
    or cx, cx
    jz .headend
    mov si, br_rxb
.hbyte:
    lodsb
    cmp al, 10
    je .lineend
    cmp al, 13
    je .hnext
    mov bx, [br_dl_linelen]
    cmp bx, BR_DL_LINE-1
    jae .badhead
    mov [br_dl_line+bx], al
    inc bx
    mov [br_dl_linelen], bx
    jmp .hnext
.lineend:
    call br_dl_headerline
    jc .badhead
    cmp byte [br_dl_state], DL_OPEN ; redirect: the rest of this receive
    je .out                         ; belongs to the old reply, drop it
    cmp byte [br_dl_headers_done], 0
    jne .afterheaders
.hnext:
    loop .hbyte
    jmp .out
.afterheaders:
    ; The LF just consumed belongs to the header. The rest of this receive
    ; begins at the body and must be staged before the shared RX buffer moves.
    dec cx
    jz .out
    cmp byte [br_dl_have_len], 0
    je .stagebody
    cmp word [br_dl_total+2], 0
    jne .stagebody
    cmp cx, [br_dl_total]
    jbe .stagebody
    mov cx, [br_dl_total]
.stagebody:
    call br_dlx_put                 ; the cluster buffer, in the URL store
    mov ax, [br_dl_chunkn]
    add [br_dl_fill], ax
    inc byte [br_dl_recvseq]       ; odd while the 32-bit counter is changing
    add [br_dl_recvbytes], ax
    adc word [br_dl_recvbytes+2], 0
    inc byte [br_dl_recvseq]       ; publish a stable even snapshot
    mov byte [br_dl_state], DL_BODY
    cmp byte [br_dl_have_len], 0
    je .out
    mov ax, [br_dl_recvbytes]
    cmp ax, [br_dl_total]
    jne .out
    mov ax, [br_dl_recvbytes+2]
    cmp ax, [br_dl_total+2]
    jne .out
    mov byte [br_dl_final], 1
    jmp .pending
.badhead:
    mov word [br_dl_msg], br_dl_s_headers
    jmp .fail
.headend:
    call br_dl_qstat
    jc .retry
    cmp ah, NSK_CLOSING
    je .shortreply
    cmp ah, NSK_CLOSED
    je .shortreply
    cmp ah, NSK_FAILED              ; reset or broken socket: no more bytes
    je .shortreply
    cmp ah, NSK_UP
    je .out
    cmp ah, NSK_CONNECT
    je .out
    jmp .out
.shortreply:
    mov word [br_dl_msg], br_dl_s_incomplete
    jmp .fail
.body:
    mov ax, [br_dl_fill]
    cmp ax, [br_dl_clustersz]
    jae .queue
    mov cx, [br_dl_clustersz]
    sub cx, ax
    cmp cx, BR_CHUNK
    jbe .cap
    mov cx, BR_CHUNK
.cap:
    cmp byte [br_dl_have_len], 0
    je .recvbody
    mov ax, [br_dl_recvbytes+2]
    cmp ax, [br_dl_total+2]
    ja .finishbody
    jb .remaining
    mov ax, [br_dl_recvbytes]
    cmp ax, [br_dl_total]
    jae .finishbody
.remaining:
    mov ax, [br_dl_total]
    mov dx, [br_dl_total+2]
    sub ax, [br_dl_recvbytes]
    sbb dx, [br_dl_recvbytes+2]
    or dx, dx
    jnz .recvbody
    cmp ax, cx
    jae .recvbody
    mov cx, ax
.recvbody:
    or cx, cx
    jz .finishbody
    cmp byte [br_nstate], BN_SEND   ; ...see .head: do not receive over it
    je .out
    call br_dl_recv
    jc .retry
    or cx, cx
    jz .bodyend
    mov si, br_rxb
    call br_dlx_put                 ; the cluster buffer, in the URL store
    mov cx, [br_dl_chunkn]         ; REP MOVSB consumed CX; retain NETV_RECV's
                                    ; byte count for both counters below
    mov ax, [br_dl_fill]
    add ax, cx
    mov [br_dl_fill], ax
    inc byte [br_dl_recvseq]       ; odd while the 32-bit counter is changing
    add [br_dl_recvbytes], cx
    adc word [br_dl_recvbytes+2], 0
    inc byte [br_dl_recvseq]       ; publish a stable even snapshot
    mov byte [br_dl_state], DL_BODY
    mov ax, [br_dl_fill]
    cmp ax, [br_dl_clustersz]
    jae .queue
    cmp byte [br_dl_have_len], 0
    je .out
    mov ax, [br_dl_recvbytes]
    cmp ax, [br_dl_total]
    jne .out
    mov ax, [br_dl_recvbytes+2]
    cmp ax, [br_dl_total+2]
    jne .out
    jmp .finishbody
.queue:
    mov byte [br_dl_final], 0
    mov ax, [br_dl_recvbytes]
    cmp ax, [br_dl_total]
    jne .queueit
    mov ax, [br_dl_recvbytes+2]
    cmp ax, [br_dl_total+2]
    jne .queueit
    mov byte [br_dl_final], 1
.queueit:
    jmp .pending
.bodyend:
    call br_dl_qstat
    jc .retry
    cmp ah, NSK_CLOSING
    je .finishbody
    cmp ah, NSK_CLOSED
    je .finishbody
    cmp ah, NSK_FAILED
    je .shortbody
    cmp ah, NSK_UP
    je .out
    cmp ah, NSK_CONNECT
    je .out
    jmp .out
.finishbody:
    cmp byte [br_dl_have_len], 0
    je .endok
    mov ax, [br_dl_recvbytes]
    cmp ax, [br_dl_total]
    jne .shortbody
    mov ax, [br_dl_recvbytes+2]
    cmp ax, [br_dl_total+2]
    jne .shortbody
.endok:
    call far [br_dl_r_sockclose]
    mov byte [br_dl_final], 1
    mov ax, [br_dl_fill]
    or ax, ax
    jz .endwait
.pending:
    mov byte [br_dl_publish], DL_PENDING ; br_dl_step sets br_dl_pending too
    jmp .out
.shortbody:
    mov word [br_dl_msg], br_dl_s_incomplete
    jmp .fail
.retry:
    cmp ax, NETE_BUSY
    je .out
    mov word [br_dl_msg], br_dl_s_http
.fail:
    call far [br_dl_r_sockclose]
    mov byte [br_dl_fail_reason], 2
.endwait:
    mov byte [br_dl_publish], DL_ENDWAIT
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

br_dl_qstat:
    mov al, [br_dl_hnd]
    mov bh, NET_CLASS
    mov bl, NETV_STATUS
    call OSAPI_DRV_CALL
    ret

br_dl_recv:
    push ax
    push bx
    push dx
    mov al, [br_dl_hnd]
    mov di, br_rxb
    mov bh, NET_CLASS
    mov bl, NETV_RECV
    call OSAPI_DRV_CALL
    pop dx
    pop bx
    pop ax
    ret

br_dl_request:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, br_dl_reqb
    mov si, br_dl_s_get
    call br_dl_rputs
    mov si, br_dl_path
    call br_dl_rputs
    mov si, br_dl_s_http10
    call br_dl_rputs
    mov si, br_dl_s_host
    call br_dl_rputs
    mov si, br_dl_host
    call br_dl_rputs
    mov si, br_dl_s_agent
    call br_dl_rputs
    mov si, br_dl_s_conn
    call br_dl_rputs
    sub di, br_dl_reqb
    mov [br_dl_reqn], di
    mov word [br_dl_sent], 0
    ; Reset only response-local fields; URL and destination survive redirects.
    mov word [br_dl_linelen], 0
    mov byte [br_dl_headers_done], 0
    mov byte [br_dl_have_len], 0
    mov byte [br_dl_transfer_encoding], 0
    mov word [br_dl_location], 0
    mov byte [br_dl_status_seen], 0
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

br_dl_rputs:
    push ax
.loop:
    lodsb
    or al, al
    jz .out
    cmp di, br_dl_reqb+BR_DL_REQMAX-1
    jae .out
    stosb
    jmp .loop
.out:
    pop ax
    ret

br_dl_headerline:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, [br_dl_linelen]
    mov byte [br_dl_line+bx], 0
    mov word [br_dl_linelen], 0
    or bx, bx
    jz .blank
    cmp byte [br_dl_status_seen], 0
    jne .field
    mov byte [br_dl_status_seen], 1
    mov si, br_dl_line
.status:
    cmp byte [si], ' '
    je .digits
    or byte [si], 0
    jz .bad
    inc si
    jmp .status
.digits:
    inc si
    mov ax, 0
    mov cx, 3
.code:
    mov bl, [si]
    cmp bl, '0'
    jb .bad
    cmp bl, '9'
    ja .bad
    sub bl, '0'
    mov dx, 10
    mul dx
    xor bh, bh
    add ax, bx
    inc si
    loop .code
    mov [br_dl_code], ax
    jmp .ok
.field:
    mov si, br_dl_line
    mov bx, br_dl_s_cl
    call br_dl_lineprefix
    jc .location
    mov si, br_dl_line+15
    call br_dl_parse_len
    jc .bad
    jmp .ok
.location:
    mov si, br_dl_line
    mov bx, br_dl_s_loc
    call br_dl_lineprefix
    jc .encoding
    mov si, br_dl_line+9
    call br_dl_skipspace
    mov di, br_dl_loc
    mov cx, BR_DL_URLMAX-1
.loccopy:
    lodsb
    or al, al
    jz .locterm
    or cx, cx
    jnz .locstore
    jmp .bad
.locstore:
    stosb
    loop .loccopy
    cmp byte [si], 0
    jne .bad
.locterm:
    mov byte [di], 0
    mov word [br_dl_location], 1
    jmp .ok
.encoding:
    mov si, br_dl_line
    mov bx, br_dl_s_te
    call br_dl_lineprefix
    jc .ok
    mov byte [br_dl_transfer_encoding], 1
    jmp .ok
.blank:
    cmp byte [br_dl_status_seen], 0
    je .bad
    cmp byte [br_dl_transfer_encoding], 0
    jne .chunked
    mov ax, [br_dl_code]
    cmp ax, 301
    je .redirect
    cmp ax, 302
    je .redirect
    cmp ax, 303
    je .redirect
    cmp ax, 307
    je .redirect
    cmp ax, 308
    je .redirect
    cmp ax, 200
    jb .httpbad
    cmp ax, 299
    ja .httpbad
    mov byte [br_dl_headers_done], 1
    mov byte [br_dl_state], DL_BODY
    mov word [br_dl_msg], br_dl_s_receiving
    jmp .ok
.redirect:
    cmp word [br_dl_location], 0
    je .redirbad
    cmp byte [br_dl_redirs], 3
    jae .redirbad
    inc byte [br_dl_redirs]
    call br_dl_resolve_redirect
    jc .redirbad
    call far [br_dl_r_sockclose]
    call br_dl_split
    jc .redirbad
    mov byte [br_dl_state], DL_OPEN
    mov byte [br_dl_status_seen], 0
    mov byte [br_dl_headers_done], 0
    mov byte [br_dl_have_len], 0
    mov byte [br_dl_transfer_encoding], 0
    jmp .ok
.chunked:
    mov word [br_dl_msg], br_dl_s_chunked
    jmp .bad
.httpbad:
    mov word [br_dl_msg], br_dl_s_httpcode
    jmp .bad
.redirbad:
    mov word [br_dl_msg], br_dl_s_redirect
.bad:
    stc
    jmp .out
.ok:
    clc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

br_dl_lineprefix:
    push ax
    push cx
    push dx
.next:
    mov al, [bx]
    or al, al
    jz .yes
    mov ah, [si]
    cmp ah, 'A'
    jb .compare
    cmp ah, 'Z'
    ja .compare
    or ah, 0x20
.compare:
    cmp al, ah
    jne .no
    inc bx
    inc si
    jmp .next
.yes:
    pop dx
    pop cx
    pop ax
    clc
    ret
.no:
    pop dx
    pop cx
    pop ax
    stc
    ret

br_dl_skipspace:
    cmp byte [si], ' '
    je .next
    cmp byte [si], 9
    jne .out
.next:
    inc si
    jmp br_dl_skipspace
.out:
    ret

br_dl_parse_len:
    push ax
    push bx
    push cx
    push dx
    push di
    call br_dl_skipspace
    mov word [br_dl_numlo], 0
    mov word [br_dl_numhi], 0
    xor cx, cx
.digit:
    mov al, [si]
    cmp al, '0'
    jb .finish
    cmp al, '9'
    ja .finish
    sub al, '0'
    xor ah, ah
    mov di, ax
    mov ax, [br_dl_numlo]
    mov bx, 10
    mul bx
    mov [br_dl_worklo], ax
    mov [br_dl_workhi], dx
    mov ax, [br_dl_numhi]
    mul bx
    or dx, dx
    jnz .bad
    add ax, [br_dl_workhi]
    jc .bad
    mov [br_dl_numhi], ax
    mov ax, [br_dl_worklo]
    add ax, di
    adc word [br_dl_numhi], 0
    jc .bad
    mov [br_dl_numlo], ax
    inc si
    inc cx
    jmp .digit
.finish:
    or cx, cx
    jz .bad
    call br_dl_skipspace
    cmp byte [si], 0
    jne .bad
    cmp byte [br_dl_have_len], 0
    je .store
    mov ax, [br_dl_numlo]
    cmp ax, [br_dl_total]
    jne .bad
    mov ax, [br_dl_numhi]
    cmp ax, [br_dl_total+2]
    jne .bad
    jmp .ok
.store:
    mov ax, [br_dl_numlo]
    mov [br_dl_total], ax
    mov ax, [br_dl_numhi]
    mov [br_dl_total+2], ax
    mov byte [br_dl_have_len], 1
.ok:
    clc
    jmp .out
.bad:
    stc
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

br_dl_resolve_redirect:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [br_dl_loc], 'h'
    jne .relative
    mov si, br_dl_loc
    mov bx, br_s_scheme
    call far [br_dl_r_prefix]
    jc .https
    mov si, br_dl_loc
    mov di, br_dl_url
    mov cx, BR_DL_URLMAX
    call far [br_dl_r_copyz]
    clc
    jmp .out
.https:
    mov si, br_dl_loc
    mov bx, br_s_schemes
    call far [br_dl_r_prefix]
    jnc .bad
.relative:
    ; Start with the current absolute URL. Root-relative replaces its path;
    ; directory-relative replaces only the last path component.
    mov si, br_dl_url
    mov di, br_dl_url
    cmp byte [br_dl_loc], '/'
    jne .directory
    add si, 7
.authority:
    lodsb
    or al, al
    jz .bad
    cmp al, '/'
    jne .authority
    mov di, si
    dec di
    jmp .append
.directory:
    mov si, br_dl_url
    xor bx, bx
.scan:
    mov al, [si]
    or al, al
    jz .cut
    cmp al, '/'
    jne .scnext
    mov bx, si
.scnext:
    inc si
    jmp .scan
.cut:
    or bx, bx
    jz .bad
    mov di, bx
    inc di
.append:
    mov si, br_dl_loc
    mov cx, BR_DL_URLMAX-1
.copy:
    lodsb
    or al, al
    jz .term
    stosb
    loop .copy
    jmp .bad
.term:
    mov byte [di], 0
    clc
    jmp .out
.bad:
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
