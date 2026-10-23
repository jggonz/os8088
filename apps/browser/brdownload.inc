; =============================================================================
; Browser 1.6b: one streamed HTTP download and its native progress window.
; The worker owns sockets; the UI task owns files and the staging claim.
; =============================================================================

%include "brdownload_const.inc"

br_dl_s_open:       db 'Open Link', 0
br_dl_s_busy:       db 'Download Manager is busy', 0
br_dl_s_cancelling: db 'Previous download is cancelling', 0
br_dl_s_nodisk:     db 'No writable destination', 0
br_dl_s_nomem:      db 'Not enough memory for disk cluster', 0
br_dl_s_nodriver:   db 'Network driver unavailable', 0
br_dl_s_nolink:     db 'Network link unavailable', 0
br_dl_s_connect:    db 'Connecting...', 0
br_dl_s_receiving:  db 'Downloading...', 0
br_dl_s_canceling:  db 'Cancelling...', 0
br_dl_s_cancelled:  db 'Cancelled; partial deleted', 0
br_dl_s_delerror:   db 'Could not delete partial file', 0
br_dl_s_complete:   db 'Download complete.', 0
br_dl_s_http:       db 'HTTP request failed', 0
br_dl_s_headers:    db 'Invalid HTTP headers', 0
br_dl_s_chunked:    db 'Chunked transfer unsupported', 0
br_dl_s_httpcode:   db 'HTTP server returned an error', 0
br_dl_s_incomplete: db 'Connection ended before file completed', 0
br_dl_s_write:      db 'Disk write failed', 0
br_dl_s_stream:     db 'Streaming writes unavailable', 0
br_dl_s_redirect:   db 'Too many or invalid redirects', 0
br_dl_s_url:        db 'Invalid download URL', 0
br_dl_s_cancel:     db 'Cancel', 0
br_dl_s_exit:       db 'Exit', 0
br_dl_s_no:         db '--', 0
br_dl_s_calc:       db '--', 0
br_dl_s_speedunit:  db ' KB/s', 0
br_dl_s_etaunit:    db ' sec', 0
br_dl_s_file:       db 'File:', 0
br_dl_s_source:     db 'Source:', 0
br_dl_s_dest:       db 'Destination:', 0
br_dl_s_status:     db 'Status:', 0
br_dl_s_bytes:      db 'Bytes:', 0
br_dl_s_speed:      db 'Speed:', 0
br_dl_s_eta:        db 'ETA:', 0
br_dl_s_server:     db 'Server', 0
br_dl_s_pc:         db 'Your PC', 0
br_dl_s_default:    db 'DOWNLOAD.BIN', 0
br_dl_ext_htm:      db 'htm', 0
br_dl_ext_html:     db 'html', 0
br_dl_ext_cgi:      db 'cgi', 0
br_dl_ext_php:      db 'php', 0
br_dl_ext_phtml:    db 'phtml', 0
br_dl_ext_shtml:    db 'shtml', 0
br_dl_ext_asp:      db 'asp', 0
br_dl_ext_aspx:     db 'aspx', 0
br_dl_ext_jsp:      db 'jsp', 0
br_dl_exttab:       dw br_dl_ext_htm, br_dl_ext_html, br_dl_ext_cgi
                    dw br_dl_ext_php, br_dl_ext_phtml, br_dl_ext_shtml
                    dw br_dl_ext_asp, br_dl_ext_aspx, br_dl_ext_jsp, 0

; File > Downloads is also the recovery path after the user closes the
; secondary manager window. It is unbound and intentionally reused.
br_dl_show:
    push ax
    push bx
    push si
    call br_dl_openwin
    pop si
    pop bx
    pop ax
    ret

; AX = link index, or FFFFh for br_ubuf's already-resolved URL. A link is
; resolved against its owning frame without changing the Browser's base URL;
; either route asks the standard Save dialog.
br_dl_link:
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
    call br_dl_onwake
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
    call br_errst
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
    call br_dl_copyz
    mov si, br_host
    mov di, br_dl_hostbak
    mov cx, BR_HOSTMAX
    call br_dl_copyz
    mov si, br_path
    mov di, br_dl_pathbak
    mov cx, BR_PATHMAX
    call br_dl_copyz
    mov ax, [br_port]
    mov [br_dl_portbak], ax

    cmp word [br_dl_index], 0FFFFh ; automatic non-page navigation
    je .direct
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
    stosb
    test al, al
    jnz .base
    mov si, br_ubuf
    call br_split
    jc .bad
.href:
    mov bx, [br_dl_index]
    shl bx, 1
    mov si, [br_lnkoff+bx]
    mov es, [br_lnkseg]
    ; An absolute URL needs no page/frame base. Copy it from the link arena
    ; directly before the generic resolver rewrites the Browser's address
    ; buffer; relative hrefs still use the normal frame-aware resolver.
    mov bx, br_s_scheme
    call br_hrefpfx
    jnc .absolute
    call br_resolve
    jc .restore
    mov si, br_ubuf
    mov di, br_dl_url
    mov cx, BR_DL_URLMAX
    push ds
    pop es
    call br_dl_copyz
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
    call br_dl_copyz
.resolved:
    cmp byte [br_dl_url], 'h'
    jne .badrestore
    mov si, br_dl_url
    mov bx, br_s_scheme
    call br_dl_prefix
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
    call br_errst
    jmp .restore
.badrestore:
    mov si, br_dl_s_url
    call br_errst
    jmp .restore
.bad:
    mov si, br_dl_s_url
    call br_errst
.restore:
    mov si, br_dl_barbak
    mov di, br_ubuf
    mov cx, BR_UBUF
    call br_dl_copyz
    mov si, br_dl_hostbak
    mov di, br_host
    mov cx, BR_HOSTMAX
    call br_dl_copyz
    mov si, br_dl_pathbak
    mov di, br_path
    mov cx, BR_PATHMAX
    call br_dl_copyz
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
br_dl_copyz:
    push ax
    push cx
    push si
    push di
    jcxz .done
    dec cx
.loop:
    lodsb
    or al, al
    jz .term
    jcxz .term
    stosb
    loop .loop
.term:
    xor al, al
    stosb
.done:
    pop di
    pop si
    pop cx
    pop ax
    ret

; SI = parsed URL path. CF=1 when its final filename suffix is not rendered
; by this browser and should go straight to the existing Save/Download flow.
; Query and fragment bytes are excluded, and a dot in a directory is ignored.
br_dl_autosave:
    push ax
    push bx
    push di
    push si
    xor di, di                      ; most recent extension start, if any
.scan:
    lodsb
    or al, al
    jz .suffix
    cmp al, '?'
    je .suffix
    cmp al, '#'
    je .suffix
    cmp al, '/'
    jne .dot
    xor di, di                      ; only the last path component matters
    jmp short .scan
.dot:
    cmp al, '.'
    jne .scan
    mov di, si                      ; first byte after the final dot
    jmp short .scan
.suffix:
    or di, di
    jz .native                      ; extensionless URLs often serve HTML
    mov dx, di                      ; candidate suffix
    mov di, br_dl_exttab
.match:
    mov bx, [di]
    or bx, bx
    jz .download                    ; no renderable suffix matched
    mov si, dx
    call br_dl_extmatch
    jnc .native
    add di, 2
    jmp short .match
.download:
    stc
    jmp short .out
.native:
    clc
.out:
    pop si
    pop di
    pop bx
    pop ax
    ret

; SI = candidate URL suffix, DS:BX = suffix literal. A match consumes the
; complete suffix and requires the URL to end at NUL, query or fragment.
br_dl_extmatch:
    push ax
    push si
    push bx
.next:
    mov al, [bx]
    or al, al
    jz .end
    mov ah, [si]
    cmp ah, 'A'
    jb .compare
    cmp ah, 'Z'
    ja .compare
    or ah, 0x20
.compare:
    cmp ah, al
    jne .no
    inc si
    inc bx
    jmp short .next
.end:
    mov al, [si]
    or al, al
    jz .yes
    cmp al, '?'
    je .yes
    cmp al, '#'
    je .yes
.no:
    pop bx
    pop si
    pop ax
    stc
    ret
.yes:
    pop bx
    pop si
    pop ax
    clc
    ret

; SI string starts with DS:BX prefix, case-insensitive. CF=0 on match.
br_dl_prefix:
    push ax
    push bx
    push si
.next:
    mov al, [bx]
    or al, al
    jz .yes
    mov ah, [si]
    cmp ah, 'A'
    jb .cmp
    cmp ah, 'Z'
    ja .cmp
    or ah, 0x20
.cmp:
    cmp al, ah
    jne .no
    inc bx
    inc si
    jmp .next
.yes:
    pop si
    pop bx
    pop ax
    clc
    ret
.no:
    pop si
    pop bx
    pop ax
    stc
    ret

; The dialog's name pointer is transient. Copy it before directory and disk APIs.
br_dl_defaultname:
    push ax
    push bx
    push cx
    push si
    push di
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

br_dl_saved:
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
    mov si, br_dl_url
    mov bx, br_s_scheme
    call br_dl_prefix
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
    call br_dl_copyz
    jmp .ok
.portpath:
    mov [br_dl_port], ax
    jmp .path
.slash:
    mov byte [di], 0
    mov si, br_dl_rootpath
    mov di, br_dl_path
    mov cx, BR_DL_PATHMAX
    call br_dl_copyz
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
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

br_dl_rootpath: db '/', 0

; Claim one destination cluster without crossing the load-start 20% reserve.
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
    call OSAPI_MEM_AVAIL
    mov [br_dl_membase], bx
    jmp .room
.base:
    mov [br_dl_membase], ax
.room:
    mov ax, [br_dl_needkb]
    call br_dl_memroom
    jc .bad
    mov ax, [br_dl_needkb]
    call OSAPI_MEM_CLAIM
    jc .bad
    mov [br_dl_bufseg], dx
    clc
    jmp .out
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
    call OSAPI_MEM_AVAIL
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

br_dl_create_file:
    push ax
    push bx
    push cx
    push dx
    push es
    push ds
    pop es
    mov si, br_dl_name
    xor bx, bx
    xor cx, cx
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
    call br_dl_partload
    jc .out
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
    mov ax, br_dl_onwake
    mov bx, [br_dl_win]
    call OSAPI_WM_ONWAKE
    mov ax, br_dl_managerclose
    mov bx, [br_dl_win]
    call OSAPI_WM_ONCLOSE
    mov ax, br_dl_timer
    mov bx, [br_dl_win]
    call OSAPI_WM_ONTIMER
    mov ax, [br_dl_win]
    mov bx, br_dl_btrec
    mov si, br_dl_onup
    mov di, br_dl_ondrag
    mov dx, br_dl_onclick
    call os88ui_btninit
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
br_dl_partload:
    push ax
    push es
    mov al, 0
    call op_fetch
    jc .bad
    mov al, 0                    ; op_fetch may clobber AL; op_seg takes index here
    call op_seg
    or ax, ax
    jz .bad
    mov es, ax
    mov ax, [es:0]
    mov [br_dl_partpaint], ax
    mov [br_dl_partpaint+2], es
    mov ax, [es:2]
    mov [br_dl_partdoc], ax
    mov [br_dl_partdoc+2], es
    mov ax, [es:4]
    mov [br_dl_partlive], ax
    mov [br_dl_partlive+2], es
    mov ax, [es:6]
    mov [br_dl_partcopy], ax
    mov [br_dl_partcopy+2], es
    clc
    jmp short .out
.bad:
    stc
.out:
    pop es
    pop ax
    ret

; Drop only after the manager window is destroyed and no paint/timer callback
; can use it. op_drop is idempotent for the primary segment's UI paths.
br_dl_partdrop:
    push ax
    mov al, 0
    call op_drop
    mov word [br_dl_partpaint], 0
    mov word [br_dl_partpaint+2], 0
    mov word [br_dl_partdoc], 0
    mov word [br_dl_partdoc+2], 0
    mov word [br_dl_partlive], 0
    mov word [br_dl_partlive+2], 0
    mov word [br_dl_partcopy], 0
    mov word [br_dl_partcopy+2], 0
    pop ax
    ret

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
    call br_dl_paint
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
    call br_dl_paint
.out:
    pop si
    pop bx
    ret

br_dl_paint:
    call far [br_dl_partpaint]
    call br_dl_button
    ret

br_dl_redraw_doc:
    call far [br_dl_partdoc]
    ret

br_dl_paint_live:
    call far [br_dl_partlive]
    ret

br_dl_button:
    push ax
    push bx
    push cx
    push dx
    mov word [br_dl_btncount], 1
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
    call os88ui_btn
    pop dx
    pop cx
    pop bx
    pop ax
    ret

br_dl_timer:
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
    call br_dl_paint
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

br_dl_onup:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, br_dl_btrec
    call os88ui_btnup
    or al, al
    jz .out
    mov al, [br_dl_state]
    ; ENDWAIT is still in-flight: the UI has not yet committed the final
    ; staged bytes or classified the result. Keep cancellation asynchronous
    ; through ENDWAIT/CANCELREQ/CANCELREADY; only a terminal result may close
    ; directly from the button callback.
    cmp al, DL_OPEN
    jb .hide
    cmp al, DL_DONE
    jb .request_cancel
    cmp al, DL_CANCEL
    je .hide
    cmp al, DL_DONE
    je .hide                       ; preserve a completed file
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
    call br_dl_partdrop
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

br_dl_managerclose:
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
    mov byte [br_dl_mgr_close_after], 1
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
br_dl_ondrag:
    mov bx, br_dl_btrec
    call os88ui_btndrag
    ret

br_dl_onclick:
    ret

; UI task callback: only here may the staged cluster reach the filesystem.
br_dl_onwake:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [br_dl_mgr_close_after], 0
    je .checkpending
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
    call br_dl_partdrop
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
    mov si, br_dl_name
    mov es, [br_dl_bufseg]
    xor bx, bx
    mov cx, [br_dl_fill]
    cmp byte [br_dl_seq], 2
    je .append
    mov al, 0
    mov di, [br_dl_token]
    call OSAPI_FILE_WRITE_SEQ
    jnc .seqok
    cmp ax, FERR_NAME
    jne .writefail
    ; kern_small has no WRITE_SEQ slot. Its native APPEND path remains safe,
    ; cluster-bounded, and slower; keep the same staging protocol.
    mov byte [br_dl_seq], 2
.append:
    mov si, br_dl_name
    mov es, [br_dl_bufseg]
    xor bx, bx
    mov cx, [br_dl_fill]
    call OSAPI_FILE_APPEND
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
    mov byte [br_dl_state], DL_DONE
    mov byte [br_dl_anim], 7
    call br_dl_speedupdate
    mov word [br_dl_msg], br_dl_s_complete
    jmp .redraw
.failedend:
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
    cmp byte [br_dl_state], DL_FAIL
    jne .mainclose
    jmp .cancel_terminal_fail
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
    mov si, br_dl_name
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
    cmp byte [br_dl_seq], 2
    je .fallback
    cmp byte [br_dl_stream], 0
    je .out
    mov si, br_dl_name
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

br_dl_freebuf:
    push ax
    push dx
    mov dx, [br_dl_bufseg]
    or dx, dx
    jz .out
    call OSAPI_MEM_FREE
    mov word [br_dl_bufseg], 0
.out:
    pop dx
    pop ax
    ret

; Main Browser close: synchronously close owned resources; the kernel then
; tears down the single worker and all instance claims together.
br_dl_mainclose:
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
    call br_abort
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
    call br_dl_partdrop
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
; =============================================================================
br_dl_step:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    cmp byte [br_dl_pending], 0
    jne .retrywake
    mov al, [br_dl_cancel_req]
    or al, al
    jz .state
    cmp byte [br_dl_state], DL_CANCELREADY
    je .out
    call br_dl_sockclose
    mov ax, [br_dl_fill]
    or ax, ax
    jz .cancelready
    mov byte [br_dl_final], 2
    mov byte [br_dl_pending], 1
    mov byte [br_dl_state], DL_PENDING
    mov bx, [br_dl_win]
    call OSAPI_WM_WAKE
    jmp .out
.cancelready:
    mov byte [br_dl_state], DL_CANCELREADY
    mov bx, [br_dl_win]
    or bx, bx
    jnz .cancelwake
    ; The manager may have been closed while the worker was reaching this
    ; state. The Browser's UI wake reaps the acknowledged cancellation.
    mov bx, [br_win]
.cancelwake:
    call OSAPI_WM_WAKE
    jmp .out
.state:
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
.retrywake:
    mov bx, [br_dl_win]
    call OSAPI_WM_WAKE
    jmp .out
.open:
    call net_find
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
    mov ax, [br_dl_fill]
    mov di, [br_dl_bufseg]
    mov es, di
    xor di, di
    add di, ax
    mov [br_dl_chunkn], cx
    cld
    rep movsb
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
    mov byte [br_dl_pending], 1
    mov byte [br_dl_state], DL_PENDING
    mov bx, [br_dl_win]
    call OSAPI_WM_WAKE
    jmp .out
.badhead:
    mov word [br_dl_msg], br_dl_s_headers
    jmp .fail
.headend:
    call br_dl_qstat
    jc .retry
    cmp ah, NSK_CLOSING
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
    call br_dl_recv
    jc .retry
    or cx, cx
    jz .bodyend
    mov ax, [br_dl_fill]
    mov di, [br_dl_bufseg]
    mov es, di
    xor di, di
    add di, ax
    mov si, br_rxb
    mov [br_dl_chunkn], cx
    cld
    rep movsb
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
    mov byte [br_dl_pending], 1
    mov byte [br_dl_state], DL_PENDING
    mov bx, [br_dl_win]
    call OSAPI_WM_WAKE
    jmp .out
.bodyend:
    call br_dl_qstat
    jc .retry
    cmp ah, NSK_CLOSING
    je .finishbody
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
    call br_dl_sockclose
    mov byte [br_dl_final], 1
    mov ax, [br_dl_fill]
    or ax, ax
    jz .emptyend
    mov byte [br_dl_pending], 1
    mov byte [br_dl_state], DL_PENDING
    mov bx, [br_dl_win]
    call OSAPI_WM_WAKE
    jmp .out
.emptyend:
    mov byte [br_dl_state], DL_ENDWAIT
    mov bx, [br_dl_win]
    call OSAPI_WM_WAKE
    jmp .out
.shortbody:
    mov word [br_dl_msg], br_dl_s_incomplete
    jmp .fail
.retry:
    cmp ax, NETE_BUSY
    je .out
    mov word [br_dl_msg], br_dl_s_http
.fail:
    call br_dl_sockclose
    mov byte [br_dl_fail_reason], 2
    mov byte [br_dl_state], DL_ENDWAIT
    mov bx, [br_dl_win]
    call OSAPI_WM_WAKE
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

br_dl_sockclose:
    push ax
    push bx
    cmp byte [br_dl_hnd], 0
    je .out
    mov al, [br_dl_hnd]
    mov bh, NET_CLASS
    mov bl, NETV_CLOSE
    call OSAPI_DRV_CALL
    mov byte [br_dl_hnd], 0
.out:
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

br_dl_s_get: db 'GET ', 0
br_dl_s_http10: db ' HTTP/1.0', 13, 10, 0
br_dl_s_host: db 'Host: ', 0
br_dl_s_agent: db 13, 10, 'User-Agent: os8088 Browser 1.6c', 0
br_dl_s_conn: db 13, 10, 'Connection: close', 13, 10, 13, 10, 0

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
    call br_dl_sockclose
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

br_dl_s_cl: db 'content-length:', 0
br_dl_s_loc: db 'location:', 0
br_dl_s_te: db 'transfer-encoding:', 0

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
    call br_dl_prefix
    jc .https
    mov si, br_dl_loc
    mov di, br_dl_url
    mov cx, BR_DL_URLMAX
    call br_dl_copyz
    clc
    jmp .out
.https:
    mov si, br_dl_loc
    mov bx, br_s_schemes
    call br_dl_prefix
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

br_dl_tpl:
    dw 40, 20, 560, 154
    dw br_dl_title, br_dl_paint, 0, 0
br_dl_title: db 'Download Manager', 0

br_dl_btnlabels: dw 0
br_dl_btnrects: times 4 dw 0
br_dl_btncount: dw 1
OS88UI_BTNREC br_dl_btrec, br_dl_btnrects, br_dl_btnlabels, 0, br_dl_btncount
