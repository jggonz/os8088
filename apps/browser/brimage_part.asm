; =============================================================================
; Browser part 1: inline images (SPEC.md 71.26)
;
; The image engine's worker step - the HTTP exchange, the src's resolution,
; the GIF's header - its decode on the shared LZW (os88lzw.inc) into a copy
; ready to blit, and the UI task's half of each hand-over. The paint is the
; primary's (brimage.inc): a page whose images are in needs no part.
;
; DS is always the primary Browser segment; every imported br_* symbol is an
; offset into it (tools/brdlsyms.py), and bri_* is this part's working state
; there (brimage_const.inc). The part's OWN data - the row buffer, the column
; map, the plans - is written through ES = [cs:PXP_SELF], its data segment,
; and read through CS (apps/pixel/pxpart.inc's rule: never written through
; CS, which is a code selector on kern_mega). A primary routine is reached
; far, through br_im_rtab.
; =============================================================================
cpu 8086                        ; THE TARGET IS AN 8088 (SPEC.md 1): a part
                                ; does not get OS88_HEADER's `cpu 8086`
bits 16
org 0

%include "os88api.inc"
%include "netpkg.inc"           ; the socket verbs; NET_CLASS is [net_cls]
%include "brdocmarks.inc"       ; D_IMGDIG: the record's digits
%include "brimage_const.inc"
%define PXP_SELF PXH_SELF        ; PiXEL's part ABI, for the decoders part 1
%include "pxpart.inc"           ; hosts (71.30) - its PXP_SELF, a DECODER's
%undef PXP_SELF                 ; header word, as PXH_SELF: ours is BRIM_SELF
%include "pxrec.inc"            ; (PXR_SZ)
%include "brimage_part_symbols.inc"  ; $(BUILD)/, by -I

    dw bri_f_step                   ; BRIM_STEP
    dw bri_f_wake                   ; BRIM_WAKE
    dw 0                            ; BRIM_SELF: br_im_partload writes it
%if ($ - $$) != BRIM_SELF + 2
%error "part 1's entry table does not match brimage_const.inc"
%endif
PXP_SELF    equ BRIM_SELF           ; pxplan1.inc's name for it

bri_f_step: call bri_step
            retf
bri_f_wake: call bri_wake
            retf

BRI_ROWMAX  equ 2048                ; a frame row, at most
BRI_TWMAX   equ 768                 ; the picture's width in a box, at most
BRI_PATHEND equ 370                 ; the request's path ends by here...
BRI_LZWPAR  equ (LZW_KB * 1024) / 16 ; the LZW table, in paragraphs
BRI_DONE    equ 0x7FFE              ; the LZW's stop: every row is in
BRI_STALE   equ 0x7FFD              ; ...or the engine was stopped
BRI_DIMMAX  equ 4096                ; a logical screen's side, at most

%define LZW_BSS     bri_lzw
%define LZW_FILL    bri_fill
%define LZW_RUN     bri_run
%include "os88lzw.inc"

; =============================================================================
; THE WORKER'S STEP. br_im_step has stored br_ibusy and banked br_igen in
; br_igen0; every write of the engine's state goes through bri_go, which
; refuses once a stop has bumped the generation
; =============================================================================
bri_step:
    push ds
    pop es                          ; ES = ours for every NETV_* buffer
    mov al, [br_ie]
    cmp al, IE_NEXT
    je bri_next
    cmp al, IE_OPEN
    je bri_open
    cmp al, IE_WAIT
    je bri_wait
    cmp al, IE_SEND
    je bri_send
    cmp al, IE_HEAD
    je bri_recv
    cmp al, IE_BODY
    je bri_recv
    cmp al, IE_DEC
    jne .anim
    jmp bri_dec
.anim:
    cmp al, IE_ANIM
    jne .aset
    jmp bri_astep
.aset:
    cmp al, IE_ASET
    jne .out
    jmp bri_aset
.out:
    ret

; bri_go - [br_ie] := AL, if the engine is still this step's. CF=1 it was
; stopped (and nothing is written). All kept
bri_go:
    call bri_gchk
    jc .ret
    mov [br_ie], al
.ret:
    ret

; bri_gchk - CF=1 if a stop has bumped the generation. All kept
bri_gchk:
    push ax
    mov al, [br_igen]
    cmp al, [br_igen0]
    pop ax
    je .ok
    stc
    ret
.ok:
    clc
    ret

; bri_kick - wake the UI task now (br_im_step posts again every pass)
bri_kick:
    mov bx, [br_win]
    call OSAPI_WM_WAKE
    ret

; bri_none - image [br_ik] keeps its alt text. All kept
bri_none:
    mov byte [bri_apend], 0         ; (and it does not animate)
    push bx
    mov bl, [br_ik]
    xor bh, bh
    call bri_ent
    mov byte [bx+IT_ST], IS_NONE
    pop bx
    ret

; bri_ent - BX = an index -> BX = its table entry. Others kept
bri_ent:
    push cx
    mov cl, 4
    shl bx, cl
    add bx, br_itab
    pop cx
    ret

; bri_hclose - give the image socket back, if there is one. All kept
bri_hclose:
    push ax
    push bx
    mov al, [br_ihnd]
    or al, al
    jz .out
    mov bh, NET_CLASS
    mov bl, NETV_CLOSE
    call OSAPI_DRV_CALL
    mov byte [br_ihnd], 0
.out:
    pop bx
    pop ax
    ret

; bri_fail - this image is none; the socket closed, on to the next
bri_fail:
    call bri_gchk
    jc .out
    call bri_none
    call bri_hclose
    mov al, IE_NEXT
    call bri_go
.out:
    ret

; --- IE_NEXT: the next waiting image the layout drew -------------------------
bri_next:
    mov ah, [br_ilgen]              ; AH = the layout's boxes as the scan finds
    xor bx, bx                      ; them (br_imgsetlaid)
.scan:
    cmp bx, BR_IMGTMAX
    jae .none
    mov si, bx
    mov cl, 4
    shl si, cl
    add si, br_itab
    cmp byte [si+IT_ST], IS_WAIT
    jne .skip
    test byte [si+IT_FLG], IF_LAID
    jnz .take
.skip:
    inc bx
    jmp .scan
.none:
    cmp ah, [br_ilgen]              ; a layout set them during the scan:
    jne .ret                        ; IE_NEXT stands, the next pass looks again
    mov al, IE_END
    cmp byte [bri_anum], 0
    je .end
    mov al, IE_AGO                  ; an animation set up: the frames (71.29)
.end:
    call bri_go
    jc .ret
    jmp bri_kick
.take:
    mov [br_ik], bl
    call bri_resolve                ; the host, the port and the request
    jnc .ok
    mov byte [si+IT_ST], IS_NONE    ; not fetchable: the next pass moves on
.ret:
    ret
.ok:
    call bri_kfind                  ; a kept copy (SPEC.md 71.28)?
    jc .fetch
    cmp byte [br_icnav], 0
    jne .cond
    mov al, IE_CACHED               ; trusted for the session: no request
    call bri_go
    jc .ret
    jmp bri_kick
.cond:
    call bri_condreq                ; Reload or the bar: asked again
.fetch:
    xor ax, ax
    mov [bri_rx], ax
    mov [bri_code], ax
    mov [bri_clen], ax
    mov [bri_sent], ax
    mov [bri_hcol], al
    mov [bri_hst], al
    mov [bri_hm], al
    mov [bri_hsp], al
    mov [bri_hasc], al
    mov [bri_vn], al
    mov byte [bri_hmask], 7
    push es
    mov es, [cs:PXP_SELF]
    mov [es:bri_lm], al
    mov [es:bri_etag], al
    pop es
    mov al, IE_OPEN
    jmp bri_go

; --- IE_OPEN ------------------------------------------------------------------
bri_open:
    call far [br_im_r_net_find]     ; the driver, and is it up?
    jc .fail
    jz .fail
    mov bh, NET_CLASS
    mov bl, NETV_OPEN
    mov si, bri_host
    mov cx, [bri_port]
    call OSAPI_DRV_CALL
    jc bri_retry
    call bri_gchk
    jc .stale
    mov [br_ihnd], al
    mov al, IE_WAIT
    jmp bri_go
.stale:                             ; opened for an engine that has stopped
    mov bh, NET_CLASS
    mov bl, NETV_CLOSE
    call OSAPI_DRV_CALL
    ret
.fail:
    jmp bri_fail

; a refused verb: the Link volume's turn is not a failure (netpkg.inc)
bri_retry:
    cmp ax, NETE_BUSY
    je .ret
    jmp bri_fail
.ret:
    ret

; bri_qstat - NETV_STATUS on the image socket: AH = its state, CX readable
bri_qstat:
    mov al, [br_ihnd]
    mov bh, NET_CLASS
    mov bl, NETV_STATUS
    call OSAPI_DRV_CALL
    ret

; --- IE_WAIT ------------------------------------------------------------------
bri_wait:
    call bri_qstat
    jc bri_retry
    cmp ah, NSK_UP
    je .up
    cmp ah, NSK_CONNECT
    je .ret
    jmp bri_fail
.up:
    mov al, IE_SEND
    jmp bri_go
.ret:
    ret

; --- IE_SEND ------------------------------------------------------------------
bri_send:
    mov si, br_reqb
    add si, [bri_sent]
    mov cx, [bri_reqn]
    sub cx, [bri_sent]
    mov al, [br_ihnd]
    mov bh, NET_CLASS
    mov bl, NETV_SEND
    call OSAPI_DRV_CALL
    jc bri_retry
    call bri_gchk
    jc .ret
    add [bri_sent], cx
    mov ax, [bri_sent]
    cmp ax, [bri_reqn]
    jb .ret                         ; it took some; the rest next pass
    mov al, IE_HEAD
    jmp bri_go
.ret:
    ret

; --- IE_HEAD and IE_BODY: one receive -----------------------------------------
bri_recv:
    mov cx, BR_CHUNK
    cmp byte [br_ie], IE_BODY
    jne .ask
    call bri_room                   ; AX = what the staging still holds
    or ax, ax
    jnz .room
    jmp bri_needgrow
.room:
    cmp ax, cx
    jae .ask
    mov cx, ax
.ask:
    mov al, [br_ihnd]
    mov di, br_rxb
    mov bh, NET_CLASS
    mov bl, NETV_RECV
    call OSAPI_DRV_CALL
    jc bri_retry
    call bri_gchk
    jc .ret
    jcxz .maybeend
    mov si, br_rxb
    cmp byte [br_ie], IE_BODY
    je .body
.hdr:
    lodsb
    call bri_hbyte                  ; CF=1: that ended the blank line
    jc .tobody
    loop .hdr
.ret:
    ret
.tobody:
    dec cx                          ; CX = the body bytes after it
    cmp word [bri_code], 304
    jne .c200
    cmp byte [bri_cond], 0
    je .fail
    call bri_hclose                 ; Not Modified: the kept copy (71.28)
    mov al, IE_CACHED
    call bri_go
    jc .ret
    jmp bri_kick
.c200:
    cmp word [bri_code], 200
    jne .fail
    cmp byte [bri_hasc], 0
    je .nolen
    cmp word [bri_clen], BR_IFMAX
    ja .fail                        ; said too long before a byte is kept
.nolen:
    mov al, IE_BODY
    call bri_go
    jc .ret
    jcxz .ret
    call bri_room                   ; the header's chunk bounded too
    cmp cx, ax
    ja .fail
.body:
    call bri_stash                  ; CF=1 longer than BR_IFMAX
    jc .fail
    cmp byte [bri_hasc], 0
    je .ret
    mov ax, [bri_rx]
    cmp ax, [bri_clen]
    jb .ret
    jmp bri_ended                   ; Content-Length's worth: done
.maybeend:                          ; nothing read is not an end (netpkg.inc)
    call bri_qstat
    jc bri_retry
    cmp ah, NSK_UP
    je .ret
    cmp ah, NSK_CONNECT
    je .ret
    or cx, cx
    jnz .ret
    cmp byte [br_ie], IE_BODY
    jne .fail                       ; closed in the headers
    cmp ah, NSK_FAILED
    je .fail                        ; broken: what arrived is cut short
    cmp byte [bri_hasc], 0
    je bri_ended
    mov ax, [bri_rx]
    cmp ax, [bri_clen]
    jb .fail
    jmp bri_ended
.fail:
    jmp bri_fail

; bri_room - AX = the bytes the staging still holds: the store past the ready
; copies, BR_IFMAX at most, less what is staged. Others kept
bri_room:
    push cx
    mov ax, [br_istpar]
    sub ax, [br_iready]
    cmp ax, BR_IFMAX / 16
    jbe .b
    mov ax, BR_IFMAX / 16
.b:
    mov cl, 4
    shl ax, cl
    sub ax, [bri_rx]
    jae .out
    xor ax, ax
.out:
    pop cx
    ret

; bri_stash - CX bytes at DS:SI onto the staging's end. CF=1 the file would
; pass BR_IFMAX. The caller has bounded CX by bri_room. AX, DX, DI clobbered
bri_stash:
    mov ax, [bri_rx]
    add ax, cx
    jc .big
    cmp ax, BR_IFMAX
    ja .big
    push es
    mov dx, [br_istseg]             ; loaded this pass: a running worker pins
    mov ax, [br_iready]             ; the store, and it parks only at the top
    call far [br_im_r_imgsub]       ; of its loop
    mov es, dx
    mov di, [bri_rx]
    add [bri_rx], cx
    cld
    rep movsb
    pop es
    clc
    ret
.big:
    stc
    ret

; the staging is full and the body is not over: the UI task grows it
bri_needgrow:
    cmp word [bri_rx], BR_IFMAX
    jb .grow
    jmp bri_fail                    ; at the cap and still coming: too long
.grow:
    mov al, IE_GROW
    call bri_go
    jc .ret
    jmp bri_kick
.ret:
    ret

; the body is in: is it a GIF this reads?
bri_ended:
    call bri_hclose
    push es
    mov es, [cs:PXP_SELF]
    mov byte [es:bri_kind], BRK_GIF
    pop es
    call bri_gifhdr
    jnc .gif
    call bri_pxsniff                ; a PNG or a JPEG (71.30)?
    jnc .gif
    jmp bri_fail
.gif:
    mov al, IE_PREP
    call bri_go
    jc .ret
    jmp bri_kick
.ret:
    ret

; --- bri_hbyte - one header byte, AL --------------------------------------------
; The status line's code (the digits after its first space), a field named
; content-length (case blind) and its digits, and the blank line that ends
; them. CF=1: AL ended the blank line. AX kept? No: AX clobbered, others kept
bri_hbyte:
    push bx
    push dx
    cmp al, 10
    je .lf
    cmp al, 13
    je .ok                          ; not counted: CR LF and a bare LF alike
    cmp byte [bri_hst], 0
    jne .field
    cmp al, ' '
    jne .sdig
    inc byte [bri_hsp]
    jmp .col
.sdig:
    cmp byte [bri_hsp], 1
    jne .col
    sub al, '0'
    cmp al, 9
    ja .col
    xor ah, ah
    mov bx, ax
    mov ax, [bri_code]
    mov dx, 10
    mul dx
    add ax, bx
    mov [bri_code], ax
    jmp .col
.field:
    mov ah, [bri_hm]
    cmp ah, 1
    je .digits
    cmp ah, 3
    je .val                         ; a validator's value, as it is
    or ah, ah
    jnz .col                        ; not this line, or read
    mov bl, [bri_hcol]
    xor bh, bh
    cmp al, 'A'
    jb .m
    cmp al, 'Z'
    ja .m
    or al, 0x20
.m:                                 ; three names at once, a bit each
    test byte [bri_hmask], 1
    jz .n1
    cmp al, [br_s_clen + bx]        ; 'content-length:', the page fetch's
    jne .x1
    cmp bl, 14                      ; the colon: its digits follow
    jne .n1
    mov byte [bri_hm], 1
    mov byte [bri_hasc], 1
    mov word [bri_clen], 0
    jmp .col
.x1:
    and byte [bri_hmask], 0FEh
.n1:
    test byte [bri_hmask], 2
    jz .n2
    cmp al, [cs:bri_s_lm + bx]
    jne .x2
    cmp bl, 13
    jne .n2
    mov byte [bri_vk], 1            ; Last-Modified's value follows
    jmp .val0
.x2:
    and byte [bri_hmask], 0FDh
.n2:
    test byte [bri_hmask], 4
    jz .n3
    cmp al, [cs:bri_s_etag + bx]
    jne .x3
    cmp bl, 4
    jne .n3
    mov byte [bri_vk], 2            ; ETag's
.val0:
    mov byte [bri_hm], 3
    mov byte [bri_vn], 0
    jmp .col
.x3:
    and byte [bri_hmask], 0FBh
.n3:
    cmp byte [bri_hmask], 0
    jne .col
    mov byte [bri_hm], 0xFF
    jmp .col
.val:
    cmp al, ' '
    jne .vput
    cmp byte [bri_vn], 0
    je .col                         ; the spaces before it
.vput:
    push es
    mov es, [cs:PXP_SELF]
    mov bx, bri_lm
    mov dl, BRI_LMMAX - 1
    cmp byte [bri_vk], 1
    je .vk
    mov bx, bri_etag
    mov dl, BRI_ETMAX - 1
.vk:
    cmp [bri_vn], dl
    jae .vfull                      ; too long: cut, and a cut one never
    mov dl, [bri_vn]                ; matches, so the server answers 200
    xor dh, dh
    add bx, dx
    mov [es:bx], al
    mov byte [es:bx + 1], 0
    inc byte [bri_vn]
.vfull:
    pop es
    jmp .col
.digits:
    cmp al, ' '
    je .col
    sub al, '0'
    cmp al, 9
    ja .dend
    xor ah, ah
    mov bx, ax
    mov ax, [bri_clen]
    cmp ax, 6553
    ja .sat
    mov dx, 10
    mul dx
    add ax, bx
    jnc .cst
.sat:
    mov ax, 0xFFFF                  ; past BR_IFMAX anyway
.cst:
    mov [bri_clen], ax
    jmp .col
.dend:
    mov byte [bri_hm], 2
.col:
    cmp byte [bri_hcol], 255
    je .ok
    inc byte [bri_hcol]
.ok:
    clc
    jmp .out
.lf:
    cmp byte [bri_hcol], 0
    jne .eol
    cmp byte [bri_hst], 0
    je .ok                          ; (an empty first line: nothing yet)
    stc
    jmp .out
.eol:
    mov byte [bri_hst], 1
    mov byte [bri_hcol], 0
    mov byte [bri_hm], 0            ; the next line is matched afresh
    mov byte [bri_hmask], 7
    jmp .ok
.out:
    pop dx
    pop bx
    ret

bri_s_lm:   db 'last-modified:'
bri_s_etag: db 'etag:'

; =============================================================================
; THE SOURCE'S RESOLUTION (SPEC.md 71.26): the request in br_reqb
; =============================================================================

; bri_resolve - SI = the entry: its src against the page's base -> bri_host,
; bri_port, and `GET path HTTP/1.0` with Host and Connection: close in
; br_reqb, [bri_reqn] long. CF=1 not something this fetches. SI kept
bri_resolve:
    push si
    push es
    mov di, br_reqb
    mov word [di], 'GE'
    mov word [di+2], 'T '
    add di, 4
    mov bp, di                      ; BP = the path's start
    mov es, [br_lnkseg]
    mov si, [si+IT_SRC]             ; ES:SI = the src
    mov al, [es:si]
    or al, al
    jz .bad
    cmp al, '#'
    je .bad
    call bri_isabs                  ; http:// - SI past it
    jnc .abs
    cmp al, '/'
    jne .rel
    cmp byte [es:si+1], '/'
    jne .root
    add si, 2                       ; //host/path
.abs:
    call bri_hostpath
    jc .bad
    jmp .tail
.root:
    call bri_base
    jc .bad
    mov di, bp                      ; /path: the base's host, this path
    jmp .copy
.rel:
    call bri_scheme
    jc .bad
    call bri_base
    jc .bad
.dir:                               ; path: past the base path's last '/'
    cmp di, bp
    jbe .copy
    cmp byte [di-1], '/'
    je .copy
    dec di
    jmp .dir
.copy:
    call bri_srccopy
    jc .bad
.tail:
    cmp di, br_reqb + BRI_PATHEND
    ja .bad                         ; the rest of the request must fit
    mov bx, br_s_http               ; ' HTTP/1.0', CR LF, 'Host: '
    call bri_puts
    jc .bad
    mov bx, bri_host
    call bri_puts
    jc .bad
    mov bx, br_s_tail               ; User-Agent, Connection: close, the blank
    call bri_puts
    jc .bad
    mov ax, di
    sub ax, br_reqb
    mov [bri_reqn], ax
    call bri_extof                  ; (TGA, PCX: no signature, 71.31)
    clc
    jmp .out
.bad:
    stc
.out:
    pop es
    pop si
    ret

; bri_putc - AL at DS:DI, DI past it. CF=1 the buffer is full. Others kept
bri_putc:
    cmp di, br_reqb + BR_CHUNK
    jae .full
    mov [di], al
    inc di
    clc
    ret
.full:
    stc
    ret

; bri_puts - the NUL string at DS:BX, through bri_putc. AX, BX clobbered
bri_puts:
.l:
    mov al, [bx]
    or al, al
    jz .ok
    call bri_putc
    jc .ret
    inc bx
    jmp .l
.ok:
    clc
.ret:
    ret

; bri_srccopy - ES:SI up to its NUL or '#' through bri_putc, a space as %20.
; CF=1 full. AX clobbered; SI past what was copied
bri_srccopy:
.l:
    mov al, [es:si]
    or al, al
    jz .ok
    cmp al, '#'
    je .ok
    inc si
    cmp al, ' '
    jne .put
    mov al, '%'
    call bri_putc
    jc .ret
    mov al, '2'
    call bri_putc
    jc .ret
    mov al, '0'
.put:
    call bri_putc
    jc .ret
    jmp .l
.ok:
    clc
.ret:
    ret

; bri_isabs - does ES:SI start `http://`, in any case? CF=0 yes and SI is past
; it; CF=1 no. Others kept
bri_isabs:
    push ax
    push bx
    xor bx, bx
.c:
    mov al, [cs:bri_s_http + bx]
    or al, al
    jz .yes
    mov ah, [es:si+bx]
    cmp ah, 'A'
    jb .x
    cmp ah, 'Z'
    ja .x
    or ah, 0x20
.x:
    cmp ah, al
    jne .no
    inc bx
    jmp .c
.yes:
    add si, bx
    pop bx
    pop ax
    clc
    ret
.no:
    pop bx
    pop ax
    stc
    ret
bri_s_http: db 'http://', 0

; bri_scheme - CF=1 if ES:SI names a scheme of its own (a ':' before any '/',
; '?' or '#'): https:, ftp:, javascript:, data:. All kept
bri_scheme:
    push ax
    push si
.l:
    mov al, [es:si]
    inc si
    or al, al
    jz .ok
    cmp al, ':'
    je .no
    cmp al, '/'
    je .ok
    cmp al, '?'
    je .ok
    cmp al, '#'
    je .ok
    jmp .l
.ok:
    clc
    jmp .out
.no:
    stc
.out:
    pop si
    pop ax
    ret

; bri_hostpath - ES:SI = `host[:port][/path]` -> bri_host, bri_port, and the
; path (or '/') at DS:DI through bri_putc. CF=1 no host, or too long.
; AX clobbered; SI past it
bri_hostpath:
    push bx
    push dx
    mov word [bri_port], 80
    mov bx, bri_host
.h:
    mov al, [es:si]
    or al, al
    jz .hend
    cmp al, '/'
    je .hend
    cmp al, '?'
    je .hend
    cmp al, '#'
    je .hend
    cmp al, ':'
    je .port
    cmp bx, bri_host + BR_IHOSTMAX - 1
    jae .bad
    mov [bx], al
    inc bx
    inc si
    jmp .h
.port:
    inc si
    mov word [bri_port], 0
.pd:
    mov al, [es:si]
    sub al, '0'
    cmp al, 9
    ja .hend
    inc si
    xor ah, ah
    push ax
    mov ax, [bri_port]
    mov dx, 10
    mul dx
    pop dx
    add ax, dx
    mov [bri_port], ax
    jmp .pd
.hend:
    mov byte [bx], 0
    cmp bx, bri_host
    je .bad
    cmp word [bri_port], 0
    jne .path
    mov word [bri_port], 80
.path:
    cmp byte [es:si], '/'
    je .pc
    mov al, '/'                     ; a bare host is its root
    call bri_putc
    jc .bad
.pc:
    call bri_srccopy
    jmp .out
.bad:
    stc
.out:
    pop dx
    pop bx
    ret

; bri_base - the page's base -> bri_host, bri_port, and its path at DS:DI
; through bri_putc: the page's <base href> when it is an absolute http URL,
; else the host and path it came from. CF=1 none (a page from a file). AX
; clobbered; SI and ES kept
bri_base:
    push si
    push es
    mov si, [br_rootbaseoff]
    cmp si, 0xFFFF
    je .page
    mov es, [br_lnkseg]
    call bri_isabs
    jc .page                        ; a relative <base>: links ignore it too
    call bri_hostpath
    jmp .out
.page:
    cmp byte [br_islocal], 0
    jne .none
    cmp byte [br_host], 0
    je .none
    push di
    push ds
    pop es
    mov si, br_host
    mov di, bri_host
    cld
.hc:
    lodsb
    stosb
    or al, al
    jnz .hc
    pop di
    mov ax, [br_port]
    mov [bri_port], ax
    mov si, br_path
.pc:
    lodsb
    or al, al
    jz .done
    call bri_putc
    jc .none
    jmp .pc
.done:
    clc
    jmp .out
.none:
    stc
.out:
    pop es
    pop si
    ret

; =============================================================================
; THE GIF's HEADER (tools/pixelsim.py's gif_header and gif_rows, to the first
; frame's code bytes)
; =============================================================================

; bri_gifhdr - the staged file -> the logical screen, the first frame, its
; palette, the transparent and background indices, the minimum code size
; and where the code bytes start. CF=1 refused, as gif_rows refuses
bri_gifhdr:
    push es
    mov dx, [br_istseg]
    mov ax, [br_iready]
    call far [br_im_r_imgsub]
    mov es, dx                      ; ES = the staged file
    mov bp, [bri_rx]                ; BP = its end
    cmp bp, 13
    jb .bad
    cmp word [es:0], 'GI'
    jne .bad
    cmp word [es:2], 'F8'
    jne .bad
    mov ax, [es:6]
    or ax, ax
    jz .bad
    cmp ax, BRI_DIMMAX
    ja .bad
    mov [bri_gw], ax
    mov ax, [es:8]
    or ax, ax
    jz .bad
    cmp ax, BRI_DIMMAX
    ja .bad
    mov [bri_gh], ax
    mov word [bri_trans], 0xFFFF
    mov al, [es:11]
    mov [bri_bgi], al
    mov si, 13
    xor bx, bx                      ; BX = the global table, 0 none
    xor dx, dx                      ; DX = its entries
    mov al, [es:10]
    test al, 0x80
    jz .blocks
    call bri_palsz                  ; CX entries, AX bytes
    mov bx, si
    mov dx, cx
    add si, ax
.blocks:                            ; the blocks before the first image
    cmp si, bp
    jae .bad
    mov al, [es:si]
    inc si
    cmp al, 0x2C
    je .desc
    cmp al, 0x21
    jne .bad
    cmp si, bp
    jae .bad
    mov ah, [es:si]                 ; AH = the extension's label
    inc si
.sub:
    cmp si, bp
    jae .bad
    mov cl, [es:si]
    inc si
    xor ch, ch
    jcxz .blocks
    cmp ah, 0xF9                    ; a graphic control extension: its key
    jne .skip
    cmp cl, 4
    jb .skip
    mov word [bri_trans], 0xFFFF
    test byte [es:si], 1
    jz .skip
    push ax
    mov al, [es:si+3]
    xor ah, ah
    mov [bri_trans], ax
    pop ax
.skip:
    add si, cx
    jc .bad
    jmp .sub
.desc:
    mov ax, si
    add ax, 9
    jc .bad
    cmp ax, bp
    ja .bad
    mov ax, [es:si]
    mov [bri_fl], ax
    mov ax, [es:si+2]
    mov [bri_ft], ax
    mov ax, [es:si+4]
    or ax, ax
    jz .bad
    cmp ax, BRI_ROWMAX
    ja .bad
    mov [bri_fw], ax
    mov ax, [es:si+6]
    or ax, ax
    jz .bad
    mov [bri_fh], ax
    mov al, [es:si+8]
    add si, 9
    mov ah, al
    and ah, 0x40
    mov [bri_ilace], ah
    test al, 0x80
    jz .gpal
    call bri_palsz                  ; a local table wins
    mov bx, si
    mov dx, cx
    add si, ax
    jmp .pal
.gpal:
    or bx, bx
    jz .bad                         ; no palette at all
.pal:
    mov [bri_pal], bx
    mov [bri_npal], dx
    cmp si, bp
    jae .bad
    mov al, [es:si]
    inc si
    cmp al, 2
    jb .bad
    cmp al, 8
    ja .bad
    mov [bri_min], al
    mov [bri_data], si
    mov ax, [bri_trans]             ; the background is the key when there
    cmp ax, 0xFFFF                  ; is one (gif_rows's bgi)
    je .bg
    mov [bri_bgi], al
.bg:
    clc
    jmp .out
.bad:
    stc
.out:
    pop es
    ret

; bri_palsz - a table's packed byte AL -> CX = 2 << (AL & 7) entries, AX =
; their bytes. Others kept
bri_palsz:
    push dx
    mov cl, al
    and cl, 7
    inc cl
    mov ax, 1
    shl ax, cl
    mov cx, ax
    mov dx, ax
    shl ax, 1
    add ax, dx
    pop dx
    ret

; =============================================================================
; THE UI TASK'S HALF (br_im_wake: no lock held)
; =============================================================================
bri_wake:
    mov al, [br_ie]
    cmp al, IE_START
    je bri_ustart
    cmp al, IE_GROW
    je bri_ugrow
    cmp al, IE_PREP
    jne .show
    jmp bri_uprep
.show:
    cmp al, IE_SHOW
    jne .cached
    jmp bri_ushow
.cached:
    cmp al, IE_CACHED
    jne .aframe
    jmp bri_ucached
.aframe:
    cmp al, IE_AFRAME
    jne .ago
    jmp bri_uaframe
.ago:
    cmp al, IE_AGO
    jne .aend
    jmp bri_uago
.aend:
    cmp al, IE_AEND
    jne .ret
    jmp bri_uaend
.ret:
    ret

; IE_START - the store: BR_IGROW paragraphs of staging to begin with
bri_ustart:
    xor ax, ax
    mov [bri_apend], al
    mov [bri_anum], al
    mov [bri_amask], ax
    mov [bri_amask + 2], ax
    mov es, [cs:PXP_SELF]
    mov di, bri_arec
    mov cx, BRI_AMAX * A_SIZE / 2
    cld
    rep stosw
    cmp byte [br_iclean], 0
    jne .swept
    call bri_sweep
.swept:
    call bri_kstart                 ; a kept file for this page (71.28)
    mov ax, BR_IGROW
    xor si, si
    test byte [br_icstate], ICS_FILE
    jz .claim
    mov si, BR_IDBUF / 16           ; its copies are painted from the file:
    add ax, si                      ; the paint buffer from the start
.claim:
    push si
    push ax
    mov cl, 6
    shr ax, cl
    call far [br_im_r_memroom]
    pop ax
    jc .refusep
    mov bl, MEMP_CLAIM | MEMP_CPU
    xor cx, cx
    call far [br_im_r_claimp]
    jc .refusep
    mov [br_istseg], dx
    mov [br_istpar], ax
    call far [br_im_r_movable]      ; br_reloc follows [br_istseg]
    pop si
    mov [br_iready], si
    mov [br_ibase], si
    mov word [br_idlen], 0
    mov word [br_idlen1], 0
    mov byte [br_ie], IE_NEXT
    ret
.refusep:
    pop si
.refuse:
    mov bx, br_itab                 ; every waiting image keeps its alt
    mov cx, BR_IMGTMAX
.n:
    cmp byte [bx+IT_ST], IS_WAIT
    jne .nn
    mov byte [bx+IT_ST], IS_NONE
.nn:
    add bx, BR_ITSZ
    loop .n
    mov byte [br_ie], IE_END
    ret

; IE_GROW - the staging is full: grow it to the file's whole length when the
; server said it, else by 4 KB
bri_ugrow:
    mov ax, [bri_rx]
    add ax, 4096
    jc .max
    cmp byte [bri_hasc], 0
    je .cap
    cmp [bri_clen], ax
    jb .cap
    mov ax, [bri_clen]
.cap:
    cmp ax, BR_IFMAX
    jbe .p
.max:
    mov ax, BR_IFMAX
.p:
    add ax, 15
    mov cl, 4
    shr ax, cl
    call bri_ustore
    jc .fail
    mov byte [br_ie], IE_BODY
    ret
.fail:
    call bri_none
    call bri_hclose
    call bri_ushrink
    mov byte [br_ie], IE_NEXT
    ret

; bri_ustore - the store to AX paragraphs past the ready copies, if it is not
; that already, within the reserve; refused, the copies no animation holds go
; to EMS or the file and it is asked once more (SPEC.md 71.26, The EMS tier),
; and then, while it is still refused, the last animation is let go, its
; block given back and its copy spilt too (71.29). CF=1 refused. [br_iready]
; may be lower after. All kept
bri_ustore:
    call bri_ustore0
    jnc .ret
    push ax
    push bx
    mov bx, ax
.drop:
    call bri_adrop                  ; CF=1 no animation is left
    jc .out
    mov ax, [br_ibase]
    call bri_compact                ; its block given back
    call bri_spill                  ; ...and its copy may go now
    mov ax, bx
    add ax, [br_iready]
    jc .drop
    call bri_grow
    jc .drop
.out:
    pop bx
    pop ax
.ret:
    ret

; bri_ustore0 - bri_ustore, but no animation is let go. All kept
bri_ustore0:
    push ax
    push bx
    mov bx, ax
    add ax, [br_iready]
    jc .spill
    call bri_grow
    jnc .out
.spill:
    call bri_spill
    jc .out
    mov ax, bx                      ; past the copies still in the store
    add ax, [br_iready]
    jc .out
    call bri_grow
.out:
    pop bx
    pop ax
    ret

; bri_adrop - the last animation still set up is let go: it stays on the
; frame it shows, and its copy may be spilt (71.29). CF=1 there is none. All
; kept
bri_adrop:
    push ax
    push di
    push es
    mov es, [cs:PXP_SELF]
    mov al, BRI_AMAX
.r:
    or al, al
    jz .none
    dec al
    call bri_arp
    cmp byte [es:di + A_ON], 0
    je .r
    mov byte [es:di + A_ON], 0
    dec byte [bri_anum]
    clc
    jmp short .out
.none:
    stc
.out:
    pop es
    pop di
    pop ax
    ret

; bri_spillable - SI = an entry: CF=0 when its copy is ready in the store and
; no animation holds it (71.29), so a spill may take it. All kept
bri_spillable:
    cmp byte [si+IT_ST], IS_READY
    jne .no
    push ax
    push cx
    push di
    push es
    mov ax, si
    sub ax, br_itab
    mov cl, 4
    shr ax, cl
    mov ah, al                      ; AH = the image
    mov es, [cs:PXP_SELF]
    xor al, al
.r:
    call bri_arp
    cmp byte [es:di + A_ON], 0
    je .n
    cmp [es:di + A_K], ah
    je .held
.n:
    inc al
    cmp al, BRI_AMAX
    jb .r
    clc
    jmp short .out
.held:
    stc
.out:
    pop es
    pop di
    pop cx
    pop ax
    ret
.no:
    stc
    ret

; bri_stotal - DX = the paragraphs of the copies a spill may take. All else
; kept
bri_stotal:
    push ax
    push bx
    push cx
    push si
    xor dx, dx
    mov si, br_itab
    mov cx, BR_IMGTMAX
.e:
    call bri_spillable
    jc .n
    mov bx, si
    call bri_csize
    add dx, ax
.n:
    add si, BR_ITSZ
    loop .e
    pop si
    pop cx
    pop bx
    pop ax
    ret

; bri_nextat - AL = a state, DX = a paragraph: SI = the entry in that state
; whose copy starts lowest at or past DX. CF=1 there is none. All else kept
bri_nextat:
    push bx
    push cx
    push di
    xor di, di                      ; DI = the best so far, 0 none
    mov bx, br_itab
    mov cx, BR_IMGTMAX
.e:
    cmp [bx+IT_ST], al
    jne .n
    cmp [bx+IT_PARA], dx
    jb .n
    or di, di
    jz .take
    push ax
    mov ax, [bx+IT_PARA]
    cmp ax, [di+IT_PARA]
    pop ax
    jae .n
.take:
    mov di, bx
.n:
    add bx, BR_ITSZ
    loop .e
    or di, di
    jz .none
    mov si, di
    clc
    jmp short .out
.none:
    stc
.out:
    pop di
    pop cx
    pop bx
    ret

; bri_grow - the store to AX paragraphs in all, if it is not that already,
; within the reserve. CF=1 refused, the store as it was. All kept
bri_grow:
    push ax
    push bx
    push cx
    push dx
    cmp ax, [br_istpar]
    jbe .ok
    push ax
    sub ax, [br_istpar]
    add ax, 63
    mov cl, 6
    shr ax, cl                      ; the KB it adds
    call far [br_im_r_memroom]
    pop ax
    jc .out
    mov dx, [br_istseg]
    call far [br_im_r_regrowp]
    jc .out
    mov [br_istseg], dx
    mov [br_istpar], ax
.ok:
    clc
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; bri_spill - the store's ready copies to EMS (SPEC.md 71.26) or, when EMS
; refuses, to the cache file (71.27). CF=1 neither took them. All kept
bri_spill:
    call bri_espill
    jnc .ret
    call bri_dspill
.ret:
    ret

; bri_espill - the copies no animation holds to the EMS arena br_imgar, one
; after another, each entry marked IS_EMS, and the store closed up round what
; is left (SPEC.md 71.26, The EMS tier). UI task only: it maps the frame and
; grows an arena. CF=1 nothing moved (no such copies, no board, no frame, the
; board said no). All kept
bri_espill:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call bri_stotal                 ; DX = their paragraphs
    or dx, dx
    jz .no
    mov ax, dx
    add ax, [br_iemsp]
    jc .no                          ; past the arena's 1 MB
    mov dx, ax
    mov cl, 10
    shr dx, cl
    test ax, 3FFh
    jz .pages
    inc dx
.pages:
    mov ax, dx                      ; AX = the arena's pages for them all
    call far [br_im_r_emsframe]
    jc .no
    mov si, br_imgar
    call far [br_im_r_argrow]
    jc .no
    mov di, [br_iemsp]              ; DI = the arena's paragraph
    mov si, br_itab
    mov dx, BR_IMGTMAX
.c:
    call bri_spillable
    jc .cn
    mov bx, si
    call bri_csize
    mov cx, ax
    mov bx, [si+IT_PARA]
    call bri_ecopy                  ; (nothing is marked yet)
    jc .no
.cn:
    add si, BR_ITSZ
    dec dx
    jnz .c
    mov di, [br_iemsp]              ; every one copied: each marked where it
    mov si, br_itab                 ; went, in the same order
    mov dx, BR_IMGTMAX
.e:
    call bri_spillable
    jc .en
    mov bx, si
    call bri_csize
    mov byte [si+IT_ST], IS_EMS
    mov [si+IT_PARA], di
    add di, ax
.en:
    add si, BR_ITSZ
    dec dx
    jnz .e
    mov [br_iemsp], di
    mov ax, [br_ibase]              ; the rest closed up
    call bri_compact
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

; bri_ecopy - CX paragraphs of the store from paragraph BX to the arena's
; paragraph DI, a page at a time through quarter 0. DI past them; CF=1 a page
; was not mapped. All else kept
bri_ecopy:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    mov si, br_imgar
.chunk:
    or cx, cx
    jz .ok
    mov ax, di
    and ax, 3FFh
    mov dx, 1024
    sub dx, ax                      ; to the page's end...
    cmp dx, cx
    jbe .n
    mov dx, cx                      ; ...or the copy's end: DX paragraphs
.n:
    push cx
    mov ax, di
    mov cl, 10
    shr ax, cl
    mov cx, ax
    xor al, al
    call far [br_im_r_armap]        ; the page in quarter 0 (SI = br_imgar)
    pop cx
    jc .out
    push cx
    push si
    push di
    push dx
    and di, 3FFh
    mov cl, 4
    shl di, cl                      ; ES:DI = the paragraph in the frame
    mov es, [br_emsseg]
    mov cx, dx
    shl cx, 1
    shl cx, 1
    shl cx, 1                       ; CX = its words
    mov dx, [br_istseg]
    mov ax, bx
    call far [br_im_r_imgsub]
    push ds
    mov ds, dx
    xor si, si
    cld
    rep movsw
    pop ds
    pop dx
    pop di
    pop si
    pop cx
    add bx, dx
    add di, dx
    sub cx, dx
    jmp .chunk
.ok:
    clc
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; bri_ushrink - the store back to its ready copies and 1 KB of staging (a
; header's chunk of body always fits). A shrink never moves. All kept
bri_ushrink:
    push ax
    push dx
    mov ax, [br_iready]
    add ax, 64
    cmp ax, [br_istpar]
    jae .out
    mov dx, [br_istseg]
    call far [br_im_r_regrowp]
    jc .out
    mov [br_istseg], dx
    mov [br_istpar], ax
.out:
    pop dx
    pop ax
    ret

; bri_pmove - CX paragraphs of the store from paragraph BX to paragraph AX,
; overlapping either way: 32 KB at a time, from the far end when it moves up.
; All kept
bri_pmove:
    push ax
    push bx
    push cx
    push dx
    push bp
    jcxz .out
    cmp ax, bx
    je .out
    jb .fwd
    add ax, cx                      ; up: from the top chunk down
    add bx, cx
    mov bp, 1
.bk:
    mov dx, cx
    cmp dx, 2048
    jbe .bn
    mov dx, 2048
.bn:
    sub ax, dx
    sub bx, dx
    call .chunk
    sub cx, dx
    jnz .bk
    jmp short .out
.fwd:
    xor bp, bp
.fw:
    mov dx, cx
    cmp dx, 2048
    jbe .fn
    mov dx, 2048
.fn:
    call .chunk
    add ax, dx
    add bx, dx
    sub cx, dx
    jnz .fw
.out:
    pop bp
    pop dx
    pop cx
    pop bx
    pop ax
    ret
; DX paragraphs from BX to AX, BP = 1 backwards. All kept
.chunk:
    push ax
    push cx
    push dx
    push si
    push di
    push es
    push dx
    mov dx, [br_istseg]
    call far [br_im_r_imgsub]
    mov es, dx
    mov dx, [br_istseg]
    mov ax, bx
    call far [br_im_r_imgsub]
    pop cx
    shl cx, 1
    shl cx, 1
    shl cx, 1                       ; CX = its words
    xor si, si
    xor di, di
    or bp, bp
    jz .go
    mov si, cx
    dec si
    shl si, 1
    mov di, si
    std
.go:
    push ds
    mov ds, dx
    rep movsw
    pop ds
    cld
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; bri_compact - AX = the new [br_ibase]: what is still in the store - each
; ready copy, each animation's block, then the staged file - moves to lie from
; AX on, in its order, and every paragraph that names one moves with it:
; IT_PARA, a record's A_BLK, A_MST, A_SAV and A_PLN, and [br_iready], the
; stage's start (SPEC.md 71.26). The store already holds it. The moves up go
; first, from the last, then the moves down from the first: an element's
; shift only falls along the store, so neither overwrites one not yet moved.
; UI task, the first phase: the list is built in bri_rowbuf, which only a
; decode, a frame and the sweep use. All kept
CE_SRC      equ 0                   ; an element: word, its paragraph
CE_SIZE     equ 2                   ; word: its paragraphs
CE_DST      equ 4                   ; word: where it goes
CE_ID       equ 6                   ; byte: 0..31 an image, 40h + r a record,
CE_N        equ 8                   ; FFh the stage
%if CE_N != 8
%error "bri_cadd shifts by three for CE_N"
%endif
bri_compact:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov bp, ax                      ; BP = the new base
    mov es, [cs:PXP_SELF]
    xor cx, cx                      ; CX = the elements
    mov si, br_itab
    xor bl, bl
.ce:
    cmp byte [si+IT_ST], IS_READY
    jne .cen
    push bx
    mov bx, si
    call bri_csize
    pop bx
    mov dx, ax
    mov ax, [si+IT_PARA]
    call bri_cadd
.cen:
    add si, BR_ITSZ
    inc bl
    cmp bl, BR_IMGTMAX
    jb .ce
    xor al, al
.cr:
    call bri_arp
    cmp byte [es:di + A_ON], 0
    je .crn
    push ax
    mov bl, al
    or bl, 40h
    mov dx, [es:di + A_PLN]
    add dx, 1024 / 16
    mov ax, [es:di + A_BLK]
    sub dx, ax
    call bri_cadd
    pop ax
.crn:
    inc al
    cmp al, BRI_AMAX
    jb .cr
    push cx
    mov ax, [bri_rx]
    call bri_paras                  ; (CL clobbered)
    pop cx
    mov dx, ax
    mov ax, [br_iready]
    mov bl, 0FFh
    call bri_cadd                   ; the stage, last
    mov di, bri_rowbuf              ; each one's place
    mov ax, bp
    push cx
.d:
    mov [es:di + CE_DST], ax
    add ax, [es:di + CE_SIZE]
    add di, CE_N
    loop .d
    pop cx
    push cx                         ; the moves up, from the last
.up:
    sub di, CE_N
    mov ax, [es:di + CE_DST]
    cmp ax, [es:di + CE_SRC]
    jbe .upn
    call bri_cmove
.upn:
    loop .up
    pop cx
.dn:                                ; the moves down, from the first
    mov ax, [es:di + CE_DST]
    cmp ax, [es:di + CE_SRC]
    jae .dnn
    call bri_cmove
.dnn:
    add di, CE_N
    loop .dn
    mov ax, [es:di - CE_N + CE_DST] ; the stage's place
    mov [br_iready], ax
    mov [br_ibase], bp
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; bri_cadd - an element (AX = its paragraph, DX = its size, BL = its id) into
; the list at ES:bri_rowbuf, CX long, in the order of its paragraphs. CX + 1;
; all else kept
bri_cadd:
    push si
    push di
    mov di, cx
    shl di, 1
    shl di, 1
    shl di, 1                       ; (CE_N)
    add di, bri_rowbuf              ; DI = the slot past the end
.sh:
    cmp di, bri_rowbuf
    je .put
    cmp [es:di - CE_N + CE_SRC], ax
    jbe .put
    mov si, [es:di - CE_N + CE_SRC] ; the one before it up a slot
    mov [es:di + CE_SRC], si
    mov si, [es:di - CE_N + CE_SIZE]
    mov [es:di + CE_SIZE], si
    mov si, [es:di - CE_N + CE_ID]
    mov [es:di + CE_ID], si
    sub di, CE_N
    jmp .sh
.put:
    mov [es:di + CE_SRC], ax
    mov [es:di + CE_SIZE], dx
    mov [es:di + CE_ID], bl
    inc cx
    pop di
    pop si
    ret

; bri_cmove - ES:DI = an element: it is moved to its place, and what names it
; with it. All kept
bri_cmove:
    push ax
    push bx
    push cx
    push dx
    push si
    mov ax, [es:di + CE_DST]
    mov bx, [es:di + CE_SRC]
    mov cx, [es:di + CE_SIZE]
    call bri_pmove
    mov dx, ax
    sub dx, bx                      ; DX = the shift
    mov bl, [es:di + CE_ID]
    cmp bl, 0FFh
    je .out                         ; the stage: [br_iready], at the end
    test bl, 40h
    jnz .rec
    xor bh, bh
    call bri_ent
    mov [bx+IT_PARA], ax
    jmp short .out
.rec:
    mov al, bl
    and al, 3Fh
    push di
    call bri_arp
    add [es:di + A_BLK], dx
    add [es:di + A_MST], dx
    add [es:di + A_PLN], dx
    cmp word [es:di + A_SAV], 0
    je .ns
    add [es:di + A_SAV], dx
.ns:
    pop di
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; bri_spnew - BX = the paragraphs a spill takes out of the store: AX = the new
; [br_ibase] - itself, or the paint buffer's 512 paragraphs when there is none
; yet - and the store grown to hold what stays from there and 1 KB of
; staging. CF=1 refused.
; Others kept
bri_spnew:
    push cx
    push dx
    mov dx, [br_ibase]
    or dx, dx
    jnz .base
    mov dx, BR_IDBUF / 16
.base:                              ; DX = the new base
    mov ax, [bri_rx]
    add ax, 15
    mov cl, 4
    shr ax, cl                      ; the stage's paragraphs,
    add ax, dx
    add ax, [br_iready]
    sub ax, [br_ibase]
    sub ax, bx                      ; what stays, after the base
    add ax, 64                      ; and bri_ushrink's 1 KB of staging
    call bri_grow
    mov ax, dx                      ; (flags: bri_grow's answer)
    pop dx
    pop cx
    ret

; bri_csize - BX = an entry -> AX = its copy's paragraphs. Others kept
bri_csize:
    push cx
    push dx
    mov al, [bx+IT_COLS]
    xor ah, ah
    cmp byte [br_bpp], 1
    je .m
    shl ax, 1
    shl ax, 1
.m:
    mov cl, [bx+IT_ROWS]
    xor ch, ch
    mul cx                          ; a row's bytes x the cells' rows: x 8
    inc ax                          ; bytes, / 16 a paragraph, rounded up
    shr ax, 1
    pop dx
    pop cx
    ret

; bri_kcreate - make this page's cache file, its index first on a kept
; volume (71.28). Standing in BRCACHE. CF=1 refused. All kept
bri_kcreate:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    mov ax, [br_icview]
    mov [br_iccur], ax
    inc word [br_icview]
    call far [br_im_r_mkname]
    mov si, br_icname
    push ds
    pop es
    xor bx, bx
    xor cx, cx
    xor dx, dx                      ; DX:CX = 0: an empty file
    call OSAPI_FILE_WRITE
    jc .out
    or byte [br_icstate], ICS_FILE
    xor ax, ax
    mov [br_ifend], ax
    mov [br_ifend+2], ax
    cmp byte [br_ickeep], 0
    je .ok
    mov es, [cs:PXP_SELF]
    mov word [es:bri_ix + IXH_MAGIC], 'BR'
    mov word [es:bri_ix + IXH_MAGIC + 2], 'C1'
    mov ax, [br_ichash]
    mov [es:bri_ix + IXH_HASH], ax
    mov ax, [br_ichash + 2]
    mov [es:bri_ix + IXH_HASH + 2], ax
    mov bx, bri_ix
    mov cx, BRI_IXSZ
    xor ax, ax
    xor dx, dx
    call OSAPI_FILE_WRITE_AT        ; the index, the file's first bytes
    jc .out
    mov ax, BRI_IXSZ
    xor dx, dx
    call bri_round                  ; the copies after its cluster
    mov [br_ifend], ax
    mov [br_ifend+2], dx
.ok:
    clc
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; bri_round - DX:AX up to a whole cluster ([bri_dclus] bytes, a power of
; two). Others kept
bri_round:
    push cx
    mov cx, [bri_dclus]
    dec cx
    add ax, cx
    adc dx, 0
    not cx
    and ax, cx
    pop cx
    ret

; bri_dspill - the copies no animation holds to the cache file, each entry
; marked IS_DISK (SPEC.md 71.27): on a kept volume those already in it are only
; marked; else as many whole copies as the disk holds are appended - the first
; such ones in the store, each run of them that lies together one append - and
; the store is closed up round what is left. The paint buffer is the store's
; first 8 KB, grown for when the copies are fewer. UI task. CF=1 nothing
; moved. All kept
IS_SPILL    equ 5                   ; an entry chosen, its copy not yet written
bri_dspill:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    test byte [br_icstate], ICS_OFF
    jnz .nojmp
    call bri_stotal                 ; DX = the copies a spill may take
    or dx, dx
    jz .nojmp
    call bri_allkept
    jc .write
    mov bx, dx                      ; all of them are in the file already
    call bri_spnew
    jc .nojmp
    push ax
    mov es, [cs:PXP_SELF]
    mov si, br_itab
    xor bx, bx
.km:
    call bri_spillable
    jc .kn
    mov di, bx
    mov al, [es:bri_ixk + di]
    call bri_slotp                  ; DI = the slot
    mov ax, [es:di + S_PARA]
    mov [si+IT_PARA], ax
    mov byte [si+IT_ST], IS_DISK
.kn:
    add si, BR_ITSZ
    inc bx
    cmp bx, BR_IMGTMAX
    jb .km
    pop ax
    jmp .reloc
.nojmp:
    jmp .no
.write:
    mov al, 1
    call far [br_im_r_icenter]
    jc .off
    call OSAPI_FILE_DFREE           ; DX:AX free, BX sectors a cluster
    jc .offback
    mov cl, 9
    shl bx, cl
    mov [bri_dclus], bx
    sub ax, bx                      ; less a cluster and 16 KB
    sbb dx, 0
    jc .short
    sub ax, 16384
    sbb dx, 0
    jc .short
    push ax                         ; the file stays under 1 MB
    push dx
    mov ax, 0
    mov dx, 10h
    sub ax, [br_ifend]
    sbb dx, [br_ifend+2]
    mov cx, ax
    mov si, dx                      ; SI:CX = what 1 MB leaves
    pop dx
    pop ax
    cmp dx, si
    jb .lim
    ja .cap
    cmp ax, cx
    jbe .lim
.cap:
    mov ax, cx
    mov dx, si
.lim:
    cmp dx, 10h
    jb .cv
    mov ax, 0FFFFh                  ; 1 MB: as many paragraphs as there are
    jmp short .room
.cv:
    mov cl, 4                       ; DX:AX bytes -> AX paragraphs
    shr ax, cl
    mov cl, 12
    shl dx, cl
    or ax, dx
.room:
    mov [bri_dcut], ax              ; the room left, in paragraphs
    mov word [bri_dbase], 0         ; ...and what is chosen
    mov word [bri_doff], 0FFFFh     ; the last chosen one's end, none yet
    xor dx, dx                      ; DX = the walk's paragraph
.sel:
    mov al, IS_READY
    call bri_nextat                 ; SI = the next ready copy
    jc .seld
    mov bx, si
    call bri_csize
    mov dx, [si+IT_PARA]
    add dx, ax                      ; past it
    call bri_spillable
    jc .sel                         ; an animation's: it stays
    mov cx, ax                      ; CX = what it costs the disk: a new
    mov di, [si+IT_PARA]            ; run after the first starts a cluster
    cmp word [bri_doff], 0FFFFh     ; on, so it pays the last one's
    je .cost                        ; rounding
    cmp di, [bri_doff]
    je .cost
    mov di, [bri_dclus]
    shr di, 1
    shr di, 1
    shr di, 1
    shr di, 1
    add cx, di
.cost:
    cmp cx, [bri_dcut]
    ja .seld                        ; the first that does not fit ends it
    sub [bri_dcut], cx
    add [bri_dbase], ax
    mov [bri_doff], dx
    mov byte [si+IT_ST], IS_SPILL
    jmp .sel
.seld:
    mov bx, [bri_dbase]
    or bx, bx
    jz .shortj                      ; not one copy fits
    call bri_spnew                  ; AX = the new base
    jc .shortj
    mov [bri_dbase], ax
    test byte [br_icstate], ICS_FILE
    jnz .append
    call bri_kcreate
    jc .offback
.append:
    mov ax, [br_ifend]
    mov dx, [br_ifend+2]
    mov [bri_doff], ax
    mov [bri_doff+2], dx
    mov es, [cs:PXP_SELF]
    xor dx, dx
.run:
    mov al, IS_SPILL
    call bri_nextat
    jc .written
    mov bx, [si+IT_PARA]            ; BX = the run's first paragraph
    mov ax, [bri_doff]              ; CX = its place in the file
    mov dx, [bri_doff+2]
    mov cl, 4
    shr ax, cl
    mov cl, 12
    shl dx, cl
    or ax, dx
    mov cx, ax
.ext:
    mov ax, [si+IT_PARA]            ; each copy's place, kept in bri_rowbuf
    sub ax, bx                      ; until every run is written
    add ax, cx
    mov di, si
    sub di, br_itab
    shr di, 1
    shr di, 1
    shr di, 1                       ; (an entry's 16 bytes, a word's 2)
    mov [es:bri_rowbuf + di], ax
    push bx
    mov bx, si
    call bri_csize
    pop bx
    mov dx, [si+IT_PARA]
    add dx, ax                      ; DX = the run's end so far
    mov al, IS_SPILL
    call bri_nextat
    jc .wrun
    cmp [si+IT_PARA], dx
    je .ext                         ; it follows on: the same run
.wrun:
    mov [bri_dcut], dx
    push dx
.piece:
    mov cx, [bri_dcut]
    sub cx, bx
    jz .wdone
    cmp cx, 2048                    ; 32 KB a piece: a whole number of any
    jbe .last                       ; cluster up to 32 KB
    mov cx, 2048
.last:
    push bx
    push cx
    push es
    mov dx, [br_istseg]
    mov ax, bx
    call far [br_im_r_imgsub]
    mov es, dx
    mov ax, cx
    mov cl, 4
    shl ax, cl
    mov cx, ax                      ; CX = the piece's bytes
    push cx
    mov ax, [bri_doff]
    mov dx, [bri_doff+2]
    mov si, br_icname
    xor bx, bx
    push ax                         ; ES:BX is the movable store: pinned for
    xor ax, ax                      ; the call (SPEC.md 66.3 rule 5)
    call far [br_im_r_pagepin]
    pop ax
    call OSAPI_FILE_WRITE_AT
    call far [br_im_r_pageunpin]    ; AX and the flags kept
    pop cx
    pop es
    jc .wfail
    add [bri_doff], cx
    adc word [bri_doff+2], 0
    pop cx
    pop bx
    add bx, cx
    jmp .piece
.wfail:
    pop cx
    pop bx
    pop dx
    jmp .offback
.wdone:
    mov ax, [bri_doff]              ; the next run at the allocated end
    mov dx, [bri_doff+2]            ; (18.4.7)
    call bri_round
    mov [bri_doff], ax
    mov [bri_doff+2], dx
    pop dx                          ; the walk on from the run's end
    jmp .run
.shortj:
    jmp .short
.written:
    call far [br_im_r_odback]
    mov es, [cs:PXP_SELF]           ; (a far call does not keep it)
    mov si, br_itab                 ; every chosen copy is in the file: each
    xor bx, bx                      ; marked, its place from bri_rowbuf
.e:
    cmp byte [si+IT_ST], IS_SPILL
    jne .en
    mov ax, [es:bri_rowbuf + bx]
    mov [si+IT_PARA], ax
    mov byte [si+IT_ST], IS_DISK
.en:
    add si, BR_ITSZ
    add bx, 2
    cmp bx, BR_IMGTMAX * 2
    jb .e
    mov ax, [bri_doff]              ; the allocated end, where the next
    mov dx, [bri_doff+2]            ; append starts
    mov [br_ifend], ax
    mov [br_ifend+2], dx
    mov ax, [bri_dbase]
.reloc:
    cmp word [br_ibase], 0
    jne .moved
    mov word [br_idlen], 0          ; the paint buffer, new and empty
    mov word [br_idlen1], 0
.moved:
    call bri_compact                ; the rest and the stage from the new base
    clc
    jmp short .out
.offback:
    call bri_unspill
    call far [br_im_r_odback]
.off:
    or byte [br_icstate], ICS_OFF   ; refused (write-protected, no folder,
    jmp short .no                   ; an I/O error): off for the page
.short:
    call bri_unspill
    call far [br_im_r_odback]       ; full: not this time, and not off
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

; bri_unspill - every entry chosen for the file and not marked is ready again,
; as it was. All kept
bri_unspill:
    push bx
    push cx
    mov bx, br_itab
    mov cx, BR_IMGTMAX
.e:
    cmp byte [bx+IT_ST], IS_SPILL
    jne .n
    mov byte [bx+IT_ST], IS_READY
.n:
    add bx, BR_ITSZ
    loop .e
    pop cx
    pop bx
    ret

; bri_allkept - CF=0 when the volume keeps pages and every copy a spill may
; take is in the file already (its slot in bri_ixk). All kept
bri_allkept:
    push ax
    push bx
    push si
    push es
    cmp byte [br_ickeep], 0
    je .no
    test byte [br_icstate], ICS_FILE
    jz .no
    mov es, [cs:PXP_SELF]
    mov si, br_itab
    xor bx, bx
.e:
    call bri_spillable              ; (an animation's copy stays: 71.29)
    jc .n
    cmp byte [es:bri_ixk + bx], BRI_SLOTS
    jae .no
.n:
    add si, BR_ITSZ
    inc bx
    cmp bx, BR_IMGTMAX
    jb .e
    clc
    jmp short .out
.no:
    stc
.out:
    pop es
    pop si
    pop bx
    pop ax
    ret

; bri_slotp - AL = a slot -> DI = it in bri_ix (ES = the part). Others kept
bri_slotp:
    push ax
    xor ah, ah
    mov di, S_SIZE
    mul di
    add ax, bri_ix + IXH_SIZE
    mov di, ax
    pop ax
    ret

; bri_hash - CX bytes at DS:SI into DX:AX, from nothing; bri_hashz the NUL
; string at DS:SI, on from DX:AX. Each byte: the 32 bits rotated 5, then
; the byte xored in - a cheap hash, and 0, a free slot, is made 1 by the
; callers. SI past them; others kept
bri_hash:
    push bx
    push cx
    xor ax, ax
    xor dx, dx
    jcxz .done
.b:
    mov bl, [si]
    inc si
    call bri_hmix
    loop .b
.done:
    pop cx
    pop bx
    ret
bri_hashz:
    push bx
.b:
    mov bl, [si]
    inc si
    or bl, bl
    jz .done
    call bri_hmix
    jmp .b
.done:
    pop bx
    ret
bri_hmix:
    push cx
    mov cx, 5
.r:
    shl ax, 1
    rcl dx, 1
    adc ax, 0
    loop .r
    xor al, bl
    pop cx
    ret

; bri_kfind - IE_NEXT, after the request is built: its hash in [bri_rh], and
; the kept copy's slot in [bri_slot]. CF=0 there is one to use (SPEC.md
; 71.28); CF=1 fetch. All kept
bri_kfind:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov byte [bri_slot], 0FFh
    mov byte [bri_cond], 0
    mov si, br_reqb
    mov cx, [bri_reqn]
    call bri_hash
    xor ax, [bri_port]              ; the port is not in the Host line
    jnz .h
    inc ax
.h:
    mov [bri_rh], ax
    mov [bri_rh+2], dx
    cmp byte [br_ickeep], 0
    je .no
    test byte [br_icstate], ICS_OFF
    jnz .no
    test byte [br_icstate], ICS_FILE
    jz .no
    mov es, [cs:PXP_SELF]
    mov bl, [br_ik]
    xor bh, bh
    cmp byte [es:bri_ixk + bx], 0FEh
    je .no                          ; the kept one did not fit: fetch it
    call bri_ixfind
    jc .no
    push di
    call bri_slotp
    cmp byte [es:di + S_ANIM], 0
    pop di
    jne .no                         ; animated: fetched, so that it plays
    mov [bri_slot], al
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

; bri_ixfind - the slot holding [bri_rh]: CF=0 AL = it; CF=1 none. ES = the
; part. Others kept
bri_ixfind:
    push di
    xor al, al
.s:
    call bri_slotp
    push ax
    mov ax, [es:di + S_HASH]
    cmp ax, [bri_rh]
    jne .n
    mov ax, [es:di + S_HASH + 2]
    cmp ax, [bri_rh + 2]
.n:
    pop ax
    je .yes
    inc al
    cmp al, BRI_SLOTS
    jb .s
    stc
    jmp short .out
.yes:
    clc
.out:
    pop di
    ret

; bri_condreq - the request asks again (SPEC.md 71.28): If-Modified-Since
; and If-None-Match from the slot, before its blank line. Nothing to ask
; with, or no room: the request as it was. All kept
bri_condreq:
    push ax
    push bx
    push di
    push si
    push es
    mov es, [cs:PXP_SELF]
    mov al, [bri_slot]
    call bri_slotp
    mov si, di                      ; ES:SI = the slot
    cmp byte [es:si + S_LM], 0
    jne .some
    cmp byte [es:si + S_ETAG], 0
    je .out
.some:
    mov di, br_reqb
    add di, [bri_reqn]
    sub di, 2                       ; over the blank line's CR LF
    cmp byte [es:si + S_LM], 0
    je .et
    mov bx, bri_s_ims
    call bri_putcs
    jc .full
    lea bx, [si + S_LM]
    call bri_putes
    jc .full
.et:
    cmp byte [es:si + S_ETAG], 0
    je .end
    mov bx, bri_s_inm
    call bri_putcs
    jc .full
    lea bx, [si + S_ETAG]
    call bri_putes
    jc .full
.end:
    mov al, 13
    call bri_putc
    jc .full
    mov al, 10
    call bri_putc
    jc .full
    mov ax, di
    sub ax, br_reqb
    mov [bri_reqn], ax
    mov byte [bri_cond], 1
    jmp short .out
.full:
    mov di, br_reqb                 ; no room: the blank line back, and the
    add di, [bri_reqn]              ; request as it was
    mov word [di - 2], 0A0Dh
.out:
    pop es
    pop si
    pop di
    pop bx
    pop ax
    ret
bri_s_ims: db 'If-Modified-Since: ', 0
bri_s_inm: db 'If-None-Match: ', 0

; bri_putcs - the NUL string at CS:BX through bri_putc; bri_putes the one
; at ES:BX, then CR LF. CF=1 full. AX, BX clobbered
bri_putcs:
    mov al, [cs:bx]
    or al, al
    jz .ok
    call bri_putc
    jc .ret
    inc bx
    jmp bri_putcs
.ok:
    clc
.ret:
    ret
bri_putes:
    mov al, [es:bx]
    or al, al
    jz .crlf
    call bri_putc
    jc .ret
    inc bx
    jmp bri_putes
.crlf:
    mov al, 13
    call bri_putc
    jc .ret
    mov al, 10
    call bri_putc
.ret:
    ret

; bri_kstart - IE_START: does this volume keep pages, and is this page one
; of them? (SPEC.md 71.28) The index read back and checked, the file this
; page's again; else a fresh index. All kept
bri_kstart:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov es, [cs:PXP_SELF]
    cld
    mov di, bri_ixk                 ; no slot for any image yet
    mov cx, BR_IMGTMAX / 2
    mov ax, 0FFFFh
    rep stosw
    call bri_ixzero
    mov byte [br_ickeep], 0
    cmp byte [br_islocal], 0
    jne .ret
    xor ax, ax                      ; the page: its host, path and port
    xor dx, dx
    mov si, br_host
    call bri_hashz
    mov si, br_path
    call bri_hashz
    xor ax, [br_port]
    jnz .ph
    inc ax
.ph:
    mov [br_ichash], ax
    mov [br_ichash + 2], dx
    call OSAPI_FILE_HERE            ; BL = our volume: a fixed one keeps
    mov al, bl                      ; pages, and only there is BRCACHE made
    call OSAPI_VOL_KIND             ; now (a floppy's waits for a spill)
    jc .ret
    cmp al, VK_FIXED
    jne .ret
    mov al, 1
    call far [br_im_r_icenter]
    jc .ret
    mov byte [br_ickeep], 1
    call OSAPI_FILE_DFREE           ; once a page: it counts the whole FAT
    jc .back
    mov cl, 9
    shl bx, cl
    mov [bri_dclus], bx
    mov [bri_dfree], ax
    mov [bri_dfree + 2], dx
    mov bx, br_icring               ; the page among the kept ones?
    xor cx, cx
.find:
    cmp cl, [br_icrn]
    jae .back
    mov ax, [bx]
    cmp ax, [br_ichash]
    jne .fn
    mov ax, [bx + 2]
    cmp ax, [br_ichash + 2]
    je .found
.fn:
    add bx, 6
    inc cx
    jmp .find
.found:
    mov ax, [bx + 4]                ; its file is this page's again
    mov [br_iccur], ax
    push es
    push ds
    pop es
    mov si, bx                      ; out of the ring: the ones after it
    lea di, [bx]                    ; move down
    add si, 6
    mov ax, BR_ICRING - 1
    sub ax, cx
    mov cx, ax
    mov ax, cx
    shl cx, 1
    add cx, ax                      ; 3 words each
    rep movsw
    pop es
    dec byte [br_icrn]
    mov ax, [br_iccur]
    call far [br_im_r_mkname]
    mov si, br_icname
    mov bx, bri_ix
    mov cx, BRI_IXSZ
    xor ax, ax
    xor dx, dx
    call OSAPI_FILE_READ_AT
    jc .stale
    cmp ax, BRI_IXSZ
    jne .stale
    cmp word [es:bri_ix + IXH_MAGIC], 'BR'
    jne .stale
    cmp word [es:bri_ix + IXH_MAGIC + 2], 'C1'
    jne .stale
    mov ax, [es:bri_ix + IXH_HASH]
    cmp ax, [br_ichash]
    jne .stale
    mov ax, [es:bri_ix + IXH_HASH + 2]
    cmp ax, [br_ichash + 2]
    jne .stale
    mov ax, [es:bri_ix + IXH_END]
    mov [br_ifend], ax
    mov ax, [es:bri_ix + IXH_END + 2]
    mov [br_ifend + 2], ax
    or byte [br_icstate], ICS_FILE
    jmp short .back
.stale:
    call far [br_im_r_odback]
    mov ax, [br_iccur]
    call far [br_im_r_icdel]        ; not what it was: gone
    call bri_ixzero
    jmp short .ret
.back:
    call far [br_im_r_odback]
.ret:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; bri_ixzero - a fresh index. ES = the part. All kept
bri_ixzero:
    push ax
    push cx
    push di
    mov di, bri_ix
    mov cx, BRI_IXSZ / 2
    xor ax, ax
    cld
    rep stosw
    pop di
    pop cx
    pop ax
    ret

; bri_kput - IE_SHOW on a kept volume: the new copy appended to the page's
; file and its slot written - the request's hash, the size it was made for,
; where it is and the validators - and the index written back, so a later
; visit finds it (SPEC.md 71.28). A copy that does not fit is not kept, and
; that is all. All kept
bri_kput:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cmp byte [br_ickeep], 0
    je .ret
    test byte [br_icstate], ICS_OFF
    jnz .ret
    mov bl, [br_ik]
    xor bh, bh
    call bri_ent
    mov si, bx                      ; SI = the entry
    call bri_csize
    mov cl, 4
    shl ax, cl
    mov [bri_dbyt], ax              ; the copy's bytes
    mov dx, [bri_dfree + 2]         ; room: the copy, a cluster and 16 KB
    mov ax, [bri_dfree]
    sub ax, [bri_dbyt]
    sbb dx, 0
    jc .ret
    sub ax, [bri_dclus]
    sbb dx, 0
    jc .ret
    sub ax, 16384
    sbb dx, 0
    jc .ret
    mov ax, [br_ifend]              ; and under 1 MB
    mov dx, [br_ifend + 2]
    add ax, [bri_dbyt]
    adc dx, 0
    cmp dx, 10h
    jae .ret
    mov al, 1
    call far [br_im_r_icenter]
    jc .ret
    test byte [br_icstate], ICS_FILE
    jnz .slot
    call bri_kcreate
    jc .offback
.slot:
    mov es, [cs:PXP_SELF]
    call bri_ixfind                 ; the same image again: its slot
    jnc .have
    xor al, al                      ; else the first free one
.free:
    call bri_slotp
    cmp word [es:di + S_HASH], 0
    jne .fnext
    cmp word [es:di + S_HASH + 2], 0
    je .have
.fnext:
    inc al
    cmp al, BRI_SLOTS
    jb .free
    jmp .back                       ; no slot: not kept
.have:
    mov [bri_slot], al
    push es
    mov dx, [br_istseg]             ; the copy, at the allocated end
    mov ax, [si+IT_PARA]
    call far [br_im_r_imgsub]
    mov es, dx
    xor bx, bx
    mov cx, [bri_dbyt]
    mov ax, [br_ifend]
    mov dx, [br_ifend + 2]
    push si
    mov si, br_icname
    push ax                         ; pinned for the call, as bri_dspill's
    xor ax, ax
    call far [br_im_r_pagepin]
    pop ax
    call OSAPI_FILE_WRITE_AT
    call far [br_im_r_pageunpin]    ; AX and the flags kept
    pop si
    pop es
    jc .offback
    mov al, [bri_slot]
    call bri_slotp                  ; ES:DI = the slot
    mov ax, [bri_rh]
    mov [es:di + S_HASH], ax
    mov ax, [bri_rh + 2]
    mov [es:di + S_HASH + 2], ax
    mov ax, [bri_gw]
    mov [es:di + S_GW], ax
    mov ax, [bri_gh]
    mov [es:di + S_GH], ax
    mov al, [si+IT_COLS]
    mov [es:di + S_COLS], al
    mov al, [si+IT_ROWS]
    mov [es:di + S_ROWS], al
    mov al, [bri_mono]
    mov [es:di + S_MONO], al
    push bx
    mov cl, [br_ik]
    call bri_abit
    and al, [bri_amask + bx]
    pop bx
    mov [es:di + S_ANIM], al        ; (non-zero: animated)
    mov ax, [br_ifend]              ; its place, in paragraphs
    mov dx, [br_ifend + 2]
    mov cl, 4
    shr ax, cl
    mov cl, 12
    shl dx, cl
    or ax, dx
    mov [es:di + S_PARA], ax
    push si
    push di
    push ds
    push es
    pop ds                          ; the validators, part to part
    mov si, bri_lm
    add di, S_LM
    mov cx, BRI_LMMAX
    rep movsb
    mov si, bri_etag
    mov cx, BRI_ETMAX
    rep movsb
    pop ds
    pop di
    pop si
    mov bl, [br_ik]
    xor bh, bh
    mov al, [bri_slot]
    mov [es:bri_ixk + bx], al
    mov ax, [br_ifend]
    mov dx, [br_ifend + 2]
    add ax, [bri_dbyt]
    adc dx, 0
    call bri_round
    mov [br_ifend], ax
    mov [br_ifend + 2], dx
    mov [es:bri_ix + IXH_END], ax
    mov [es:bri_ix + IXH_END + 2], dx
    mov bx, bri_ix                  ; the index back: INSIDE what the file
    mov cx, BRI_IXSZ                ; has, one write (18.4.7)
    xor ax, ax
    xor dx, dx
    mov si, br_icname
    call OSAPI_FILE_WRITE_AT
    jc .offback
    mov ax, [bri_dbyt]              ; what the disk has left
    add ax, [bri_dclus]
    sub [bri_dfree], ax
    sbb word [bri_dfree + 2], 0
.back:
    call far [br_im_r_odback]
.ret:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.offback:
    call far [br_im_r_odback]
    or byte [br_icstate], ICS_OFF
    jmp short .ret

; IE_CACHED - the kept copy (SPEC.md 71.28): the box sized from the slot's
; picture, as the header would size it, and, when it is the box the copy was
; made for, the entry IS_DISK on the file's copy, the record written and the
; page laid out as the header would have, then drawn. Otherwise the image
; is fetched after all
bri_ucached:
    mov word [bri_rx], 0            ; no stage
    mov es, [cs:PXP_SELF]
    mov al, [bri_slot]
    call bri_slotp
    mov ax, [es:di + S_GW]
    mov [bri_gw], ax
    mov ax, [es:di + S_GH]
    mov [bri_gh], ax
    call bri_size
    jc .fetch
    mov es, [cs:PXP_SELF]
    mov al, [bri_slot]
    call bri_slotp
    mov ax, [bri_bc]
    cmp al, [es:di + S_COLS]
    jne .fetch
    mov al, [bri_br]
    cmp al, [es:di + S_ROWS]
    jne .fetch
    mov al, [bri_mono]
    cmp al, [es:di + S_MONO]
    jne .fetch
    cmp word [br_ibase], 0
    jne .buf
    xor bx, bx                      ; the paint buffer, the copies up past it
    call bri_spnew
    jc .fetch
    mov word [br_idlen], 0
    mov word [br_idlen1], 0
    call bri_compact
.buf:
    call bri_urecord
    mov es, [cs:PXP_SELF]
    mov al, [bri_slot]
    call bri_slotp
    mov ax, [es:di + S_PARA]
    mov bl, [br_ik]
    xor bh, bh
    mov cl, [bri_slot]
    mov [es:bri_ixk + bx], cl
    call bri_ent
    mov [bx+IT_PARA], ax
    mov ax, [bri_bc]
    mov [bx+IT_COLS], al
    mov al, [bri_br]
    mov [bx+IT_ROWS], al
    mov byte [bx+IT_ST], IS_DISK
    jmp bri_ushow
.fetch:
    mov es, [cs:PXP_SELF]
    mov bl, [br_ik]
    xor bh, bh
    mov byte [es:bri_ixk + bx], 0FEh  ; the next IE_NEXT fetches it
    mov byte [br_ie], IE_NEXT
    ret

; bri_sweep - once an instance's life, at its first IE_START: when no other
; instance bears our name, every file in BRCACHE goes - what a crash or a
; power-off left there (SPEC.md 71.27). The snapshot is read into the row
; buffer. All kept
bri_sweep:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov byte [br_iclean], 1
    mov es, [cs:PXP_SELF]
    mov di, bri_rowbuf
    call OSAPI_SYS_SNAPSHOT         ; BX = INST_MAX
    mov cx, bx
    mov si, bri_rowbuf + SS_INST
    xor di, di                      ; DI = our record, 0 = not found
    mov dx, ds
.self:
    cmp byte [es:si+SSI_STATE], 0
    je .sn
    cmp [es:si+SSI_SEG], dx
    jne .sn
    mov di, si
.sn:
    add si, SSI_RECSZ
    loop .self
    or di, di
    jz .done                        ; not ourselves found: nothing is swept
    mov cx, bx
    mov si, bri_rowbuf + SS_INST
.peer:
    cmp si, di
    je .pn
    cmp byte [es:si+SSI_STATE], 0
    je .pn
    push si
    push di
    push cx
    add si, SSI_NAME
    add di, SSI_NAME
    mov cx, 16
    push ds
    push es
    pop ds
    cld
    repe cmpsb
    pop ds
    pop cx
    pop di
    pop si
    je .done                        ; another Browser runs: its files stay
.pn:
    add si, SSI_RECSZ
    loop .peer
    xor al, al
    call far [br_im_r_icenter]
    jc .done
    xor cx, cx
.find:
    push ds
    pop es
    mov di, od_find
    call OSAPI_FILE_FIND            ; CX = the next ordinal
    jc .home
    cmp word [od_find + 14], OSAPI_FT_DIR
    je .find
    push cx
    mov si, od_find
    call OSAPI_FILE_DELETE
    pop cx
    jc .find                        ; not deleted: step past it
    xor cx, cx                      ; deleted: the folder from the top again
    jmp .find
.home:
    call far [br_im_r_odback]
.done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; IE_PREP - the box's size from the header, the record and the page laid out
; again if it changed, and the scratch for the decode
bri_uprep:
    cmp byte [cs:bri_kind], BRK_GIF
    je .gif
    jmp bri_pxprep                  ; a decoder's (71.30)
.gif:
    call bri_size
    jc .none
    call bri_urecord
    call bri_isanim                 ; animated (71.29)?
    call bri_aplan                  ; CF=0 it is to play: [bri_apar] its block
    jc .still
    mov ax, [bri_rbytes]
    call bri_paras
    mov dx, ax                      ; DX = the copy's paragraphs
    add ax, [bri_apar]
    jc .still0
    add ax, BRI_LZWPAR
    jc .still0
    call bri_ustore0                ; (no animation let go for this one)
    jc .still0
    mov ax, [bri_rx]                ; the file up past where the copy goes:
    call bri_paras                  ; the decode writes the copy in its
    mov cx, ax                      ; place, and the file stays behind it
    mov bx, [br_iready]             ; as the block's start
    mov ax, bx
    add ax, dx
    call bri_pmove
    mov [bri_newp], bx
    add ax, [bri_apar]
    mov [bri_lzwp], ax              ; the table past the block
    mov byte [br_ie], IE_DEC
    ret
.still0:
    mov byte [bri_apend], 0         ; refused: decoded as a still one
.still:
    mov ax, [bri_rx]
    add ax, 15
    mov cl, 4
    shr ax, cl
    mov dx, ax                      ; DX = the staged file's paragraphs
    add ax, BRI_LZWPAR
    mov bx, [bri_rbytes]
    add bx, 15
    shr bx, cl
    add ax, bx
    jc .none
    call bri_ustore                 ; (the copies may go to EMS here)
    jc .none
    add dx, [br_iready]
    mov [bri_lzwp], dx              ; the table after the staged file...
    add dx, BRI_LZWPAR
    mov [bri_newp], dx              ; ...and the new copy after the table
    mov byte [br_ie], IE_DEC
    ret
.none:
    call bri_none
    call bri_ushrink
    mov byte [br_ie], IE_NEXT
    ret

; bri_size - the picture's size in its box, the box, and the copy's bytes
; (SPEC.md 71.26, The size). CF=1 the copy would pass BR_IRMAX
bri_size:
    mov bl, [br_ik]
    xor bh, bh
    call bri_ent
    mov al, [bx+IT_FLG]
    mov cl, IF_BSHIFT
    shr al, cl
    mov [bri_bord], al
    mov ax, [bx+IT_W]               ; declared, 0 = not given
    mov dx, [bx+IT_H]
    or ax, ax
    jz .now
    or dx, dx
    jz .onlyw
    mov [bri_tw], ax                ; both: as they are
    mov [bri_th], dx
    jmp .cut
.onlyw:
    mov [bri_tw], ax                ; width: the height to the aspect
    mov bx, [bri_gh]
    mov cx, [bri_gw]
    call bri_muldiv
    mov [bri_th], ax
    jmp .cut
.now:
    or dx, dx
    jz .natural
    mov [bri_th], dx                ; height: the width to the aspect
    mov ax, dx
    mov bx, [bri_gw]
    mov cx, [bri_gh]
    call bri_muldiv
    mov [bri_tw], ax
    jmp .cut
.natural:
    mov ax, [bri_gw]
    mov [bri_tw], ax
    mov ax, [bri_gh]
    mov [bri_th], ax
.cut:                               ; the box fits BR_MAXCOL x BR_IMGRMAX
    mov al, [bri_bord]
    xor ah, ah
    shl ax, 1
    mov bx, BRI_TWMAX
    sub bx, ax                      ; BX = the widest picture
    mov cx, [bri_tw]
    cmp cx, bx
    jbe .wok
    mov ax, [bri_th]                ; th = th x maxw / tw
    push bx
    call bri_muldiv
    pop bx
    mov [bri_th], ax
    mov [bri_tw], bx
.wok:
    mov al, [bri_bord]
    xor ah, ah
    shl ax, 1
    mov bx, BR_IMGRMAX * BR_CELL
    sub bx, ax                      ; BX = the tallest
    mov cx, [bri_th]
    cmp cx, bx
    jbe .hok
    mov ax, [bri_tw]
    push bx
    call bri_muldiv
    pop bx
    mov [bri_tw], ax
    mov [bri_th], bx
.hok:
    mov al, [bri_bord]
    xor ah, ah
    shl ax, 1
    mov dx, ax                      ; DX = both borders
    mov ax, [bri_tw]
    add ax, dx
    add ax, 7
    mov cl, 3
    shr ax, cl
    mov [bri_bc], ax                ; COLS
    mov bx, ax                      ; BX = the stride: COLS...
    mov ax, [bri_th]
    add ax, dx
    add ax, 7
    shr ax, cl
    mov [bri_br], al                ; ROWS
    mov byte [bri_mono], 1
    cmp byte [br_bpp], 1
    je .mono
    mov byte [bri_mono], 0
    shl bx, 1                       ; ...four planes of it
    shl bx, 1
.mono:
    mov [bri_stride], bx
    xor ah, ah
    shl ax, cl                      ; the box's pixel rows
    mul bx
    or dx, dx
    jnz .big
    cmp ax, BR_IRMAX
    ja .big
    mov [bri_rbytes], ax
    clc
    ret
.big:
    stc
    ret

; bri_muldiv - AX x BX / CX, at least 1, FFFFh when it does not fit. DX
; clobbered
bri_muldiv:
    mul bx
    cmp dx, cx
    jae .sat
    div cx
    or ax, ax
    jnz .ret
    inc ax
.ret:
    ret
.sat:
    mov ax, 0xFFFF
    ret

; bri_urecord - the record's COLS and ROWS := the box's; if that changed, the
; page laid out again and put up (header-first sizing). Under the gfx lock:
; the document can share the EMS frame
bri_urecord:
    call OSAPI_GFX_LOCK
    mov bl, [br_ik]
    xor bh, bh
    call bri_ent
    mov ax, [bx+IT_DOC]
    mov dx, [bx+IT_DOC+2]
    add ax, 1                       ; past D_IMG
    adc dx, 0
    call far [br_im_r_docmap]       ; ES:DI = its COLS digits
    mov si, di
    mov cx, 3
    call bri_dig
    jc .out                         ; not a record: leave it
    cmp ax, [bri_bc]
    jne .write
    mov cx, 3
    call bri_dig
    jc .out
    cmp al, [bri_br]
    je .out
.write:
    mov ax, [bri_bc]
    mov cx, 3
    call bri_put6
    mov al, [bri_br]
    xor ah, ah
    mov cx, 3
    call bri_put6
    call far [br_im_r_layout]       ; the page again, at the box's real size
    xor ax, ax
    call far [br_im_r_advance]      ; ...and the view inside it
    mov bx, [br_win]
    call OSAPI_WM_CLIP_SET          ; covered: the kernel's repaint draws it
    jc .out
    call far [br_im_r_paint_band]
    call far [br_im_r_sbar]
.out:
    call OSAPI_GFX_UNLOCK
    ret

; bri_dig - CX base-6 digits at ES:SI -> AX, SI past them. CF=1 a byte that
; is not one (br_imgdig's rule). BX, DX kept
bri_dig:
    push bx
    push dx
    xor ax, ax
    mov bx, 6
.d:
    mul bx
    mov dl, [es:si]
    inc si
    sub dl, D_IMGDIG
    cmp dl, 6
    jae .bad
    xor dh, dh
    add ax, dx
    loop .d
    clc
    jmp .out
.bad:
    stc
.out:
    pop dx
    pop bx
    ret

; bri_put6 - AX as CX base-6 digits at ES:DI, most significant first, DI past
; them (br_imgput's encoding). AX, CX, DX clobbered
bri_put6:
    ; STKBALANCE-LOOP: one digit pushed a turn and the second loop pops them; the count is in CX
    push bx
    push si
    mov si, cx
    mov bx, 6
.div:
    xor dx, dx
    div bx
    push dx
    loop .div
    mov cx, si
.emit:
    pop ax
    add al, D_IMGDIG
    mov [es:di], al
    inc di
    loop .emit
    pop si
    pop bx
    ret

; IE_SHOW - the scratch given back, and the image drawn where it shows: an
; images-only pass, which blits its slices and nothing else
bri_ushow:
    cmp byte [bri_apend], 0
    jne .held                       ; the block and the table stay (71.29)
    call bri_ushrink
.held:
    mov bl, [br_ik]
    xor bh, bh
    call bri_ent
    cmp byte [bx+IT_ST], IS_DISK
    je .draw                        ; a kept copy, from the file
    cmp byte [bx+IT_ST], IS_READY
    jne .next
    call bri_kput                   ; a kept volume keeps it (71.28)
.draw:
    call OSAPI_GFX_LOCK
    mov bx, [br_win]
    call OSAPI_WM_CLIP_SET
    jc .unlock
    mov al, [br_ik]
    inc al
    mov [br_ionly], al
    call far [br_im_r_paint_band]
    mov byte [br_ionly], 0
.unlock:
    call OSAPI_GFX_UNLOCK
.next:
    mov al, IE_NEXT
    cmp byte [bri_apend], 0
    je .go
    mov al, IE_ASET                 ; its animation, from the file in hand
.go:
    mov [br_ie], al
    ret

; =============================================================================
; THE DECODE (IE_DEC, on the worker): the plans, the column map, the copy's
; ground, the frame through the LZW, and the copy moved down
; =============================================================================
bri_dec:
    cmp byte [cs:bri_kind], BRK_GIF
    je .gif
    jmp bri_pxdec                   ; a decoder's (71.30)
.gif:
    mov ax, [br_iready]             ; the staged file: at the stage, or past
    cmp byte [bri_apend], 0         ; the copy's place when it will animate
    je .stg
    mov ax, [bri_rbytes]
    call bri_paras
    add ax, [bri_newp]
.stg:
    mov dx, [br_istseg]
    call far [br_im_r_imgsub]
    mov [bri_stgs], dx
    mov dx, [br_istseg]
    mov ax, [bri_lzwp]
    call far [br_im_r_imgsub]
    mov [bri_lzws], dx
    mov dx, [br_istseg]
    mov ax, [bri_newp]
    call far [br_im_r_imgsub]
    mov [bri_news], dx
    call bri_plans
    jc .stale
    call bri_colmap
    call bri_ground
    call bri_bgrows
    jc .stale
    mov ax, [bri_data]
    mov [bri_fpos], ax
    xor ax, ax
    mov [bri_blk], al
    mov [bri_bend], al
    mov [bri_fcol], ax
    mov [bri_frow], ax
    mov [bri_fy], ax
    mov [bri_pass], al
    mov al, [bri_min]
    mov es, [bri_lzws]
    call lzw_decode
    push ds
    pop es
    jnc .short                      ; the codes ended before the last row
    cmp ax, BRI_STALE
    je .stale
    cmp ax, BRI_DONE
    jne .short                      ; a code the table never built
.done:                              ; (bri_pxdec's end too)
    cmp byte [bri_apend], 0
    jne .inplace                    ; written where it stays (71.29)
    call bri_movedown
.inplace:
    call bri_gchk
    jc .stale
    mov bl, [br_ik]
    xor bh, bh
    call bri_ent
    mov ax, [br_iready]
    mov [bx+IT_PARA], ax
    mov ax, [bri_bc]
    mov [bx+IT_COLS], al
    mov al, [bri_br]
    mov [bx+IT_ROWS], al
    mov ax, [bri_rbytes]
    add ax, 15
    mov cl, 4
    shr ax, cl
    add [br_iready], ax
    mov byte [bx+IT_ST], IS_READY
    jmp .show
.short:
    call bri_none
.show:
    mov al, IE_SHOW
    call bri_go
    jc .stale
    jmp bri_kick
.stale:
    ret

; bri_plans - each palette entry's plan (c1, c2, t) and 1bpp threshold into
; the part's own tables: PiXEL's search and threshold over the staged file's
; RGB, white for the transparent entry, black past the palette. CF=1 stopped
bri_plans:
    push es
    mov es, [cs:PXP_SELF]
    mov di, bri_ptab
    xor bx, bx
.e:
    cmp bx, [bri_trans]
    je .white
    cmp bx, [bri_npal]
    jae .black
    mov ax, bx
    shl ax, 1
    add ax, bx
    add ax, [bri_pal]
    mov si, ax                      ; the entry's RGB in the file
    mov cx, [bri_stgs]
    push ds
    mov ds, cx
    push dx
    call pl_plan1                   ; AL = c1, AH = c2, DL = t
    mov [es:di], al
    mov [es:di+1], ah
    mov [es:di+2], dl
    call px_t1for                   ; AL = t1
    mov [es:bri_t1 + bx], al
    pop dx
    pop ds
    jmp .n
.white:
    mov word [es:di], 0x0F0F
    mov byte [es:di+2], 0
    mov byte [es:bri_t1 + bx], 64
    jmp .n
.black:
    mov word [es:di], 0
    mov byte [es:di+2], 0
    mov byte [es:bri_t1 + bx], 0
.n:
    add di, 3
    test bl, 15
    jnz .nx
    call bri_gchk
    jc .out
.nx:
    inc bx
    cmp bx, 256
    jb .e
    clc
.out:
    pop es
    ret

; bri_colmap - each box column's frame column: tx W / tw - left, FFFFh off
; the frame
bri_colmap:
    push es
    mov es, [cs:PXP_SELF]
    mov di, bri_cmap
    xor bx, bx
    cld
.l:
    cmp bx, [bri_tw]
    jae .out
    mov ax, bx
    mul word [bri_gw]
    div word [bri_tw]               ; tx < tw: the quotient is below W
    sub ax, [bri_fl]
    jb .off
    cmp ax, [bri_fw]
    jb .st
.off:
    mov ax, 0xFFFF
.st:
    stosw
    inc bx
    jmp .l
.out:
    pop es
    ret

; bri_ground - the copy white, its border black
bri_ground:
    push es
    mov es, [bri_news]
    xor di, di
    mov cx, [bri_rbytes]
    mov al, 0xFF
    cld
    rep stosb
    mov al, [bri_bord]
    xor ah, ah
    or ax, ax
    jz .out
    mov si, ax                      ; SI = b
    mov dx, [bri_tw]
    add dx, ax
    add dx, ax                      ; DX = the ring's width
    mov bp, [bri_th]
    add bp, ax
    add bp, ax                      ; BP = its height
    xor bx, bx                      ; BX = the row
.row:
    cmp bx, bp
    jae .out
    mov ax, bx
    sub ax, si
    jb .whole                       ; above the picture
    cmp ax, [bri_th]
    jae .whole                      ; below it
    xor cx, cx                      ; beside it: the two sides
    mov ax, si
    call bri_blacks
    mov cx, si
    add cx, [bri_tw]
    mov ax, dx
    call bri_blacks
    jmp .nr
.whole:
    xor cx, cx
    mov ax, dx
    call bri_blacks
.nr:
    inc bx
    jmp .row
.out:
    pop es
    ret

; bri_blacks - row BX's pixels CX up to AX black. ES = the copy. All kept
bri_blacks:
    push ax
    push cx
    push dx
    mov dx, ax
.l:
    cmp cx, dx
    jae .out
    xor al, al
    call bri_px
    inc cx
    jmp .l
.out:
    pop dx
    pop cx
    pop ax
    ret

; bri_px - pixel CX of box row BX := AL (a colour; in 1bpp 1 lit). ES = the
; copy. All but AX kept
bri_px:
    push bx
    push cx
    push dx
    push di
    push ax
    mov ax, bx
    mul word [bri_stride]
    mov di, ax
    mov ax, cx
    shr ax, 1
    shr ax, 1
    shr ax, 1
    add di, ax                      ; ES:DI = plane 0's byte
    and cl, 7
    mov dl, 0x80
    shr dl, cl                      ; DL = the bit
    mov dh, dl
    not dh                          ; DH = the rest
    pop ax
    mov cx, 1
    cmp byte [bri_mono], 0
    jne .p
    mov cl, 4
.p:
    shr al, 1
    jnc .clr
    or [es:di], dl
    jmp .pn
.clr:
    and [es:di], dh
.pn:
    add di, [bri_bc]
    loop .p
    pop di
    pop dx
    pop cx
    pop bx
    ret

; bri_bgrows - when the frame does not cover the screen, every row of the
; picture the background's first (gif_rows: the rest of the screen is the
; background index). CF=1 stopped
bri_bgrows:
    cmp word [bri_fl], 0
    jne .fill
    cmp word [bri_ft], 0
    jne .fill
    mov ax, [bri_fw]
    cmp ax, [bri_gw]
    jb .fill
    mov ax, [bri_fh]
    cmp ax, [bri_gh]
    jb .fill
    clc
    ret
.fill:
    mov byte [bri_allbg], 1
    call bri_trowfill
    xor bx, bx
.l:
    cmp bx, [bri_th]
    jae .done
    call bri_gchk
    jc .out
    call bri_convrow
    inc bx
    jmp .l
.done:
    clc
.out:
    mov byte [bri_allbg], 0
    ret

; --- LZW_FILL: the sub-blocks' bytes into ES:DI, CX of room. out CX = the
; bytes put there; 0 once the terminator or the file's end has been met
bri_fill:
    xor bx, bx                      ; BX = put so far
.l:
    cmp byte [bri_bend], 0
    jne .done
    cmp byte [bri_blk], 0
    jne .copy
    mov si, [bri_fpos]              ; a sub-block's length
    cmp si, [bri_rx]
    jae .stop
    mov dx, [bri_stgs]
    push ds
    mov ds, dx
    mov al, [si]
    pop ds
    inc word [bri_fpos]
    or al, al
    jz .stop                        ; the terminator
    mov [bri_blk], al
.copy:
    mov si, [bri_fpos]
    mov ax, [bri_rx]
    sub ax, si                      ; the file's bytes...
    jbe .stop
    mov dl, [bri_blk]
    xor dh, dh
    cmp ax, dx                      ; ...the block's...
    jbe .a1
    mov ax, dx
.a1:
    mov dx, cx
    sub dx, bx                      ; ...the room's
    cmp ax, dx
    jbe .a2
    mov ax, dx
.a2:
    add [bri_fpos], ax
    sub [bri_blk], al
    add bx, ax
    push cx
    mov cx, ax
    mov dx, [bri_stgs]
    push ds
    mov ds, dx
    cld
    rep movsb
    pop ds
    pop cx
    cmp bx, cx
    jb .l
    jmp .done
.stop:
    mov byte [bri_bend], 1
.done:
    mov cx, bx
    ret

; --- LZW_RUN: ES:SI = a string of CX indices, into the frame's rows. A row
; whole goes to bri_rowdone; CF=1 AX = BRI_DONE when the last is in, or
; BRI_STALE. Keeps DX and BP (os88lzw.inc's contract)
bri_run:
    push dx
    push bp
.chunk:
    jcxz .ok
    mov ax, [bri_fw]
    sub ax, [bri_fcol]              ; what is left of this row (> 0)
    cmp ax, cx
    jbe .take
    mov ax, cx
.take:                              ; AX = this chunk
    push cx
    mov cx, ax
    mov di, [bri_fcol]
    add di, bri_rowbuf
    add [bri_fcol], ax
    mov bx, [cs:PXP_SELF]
    push ds
    push es
    push es
    pop ds                          ; DS:SI = the table's string
    mov es, bx                      ; ES:DI = the row buffer
    cld
    rep movsb
    pop es
    pop ds
    pop cx
    sub cx, ax
    mov ax, [bri_fcol]
    cmp ax, [bri_fw]
    jb .chunk
    mov word [bri_fcol], 0
    call bri_rowdone
    jc .out
    jmp .chunk
.ok:
    clc
.out:
    pop bp
    pop dx
    ret

; bri_rowdone - frame row [bri_fy] is whole: into every box row it fills, then
; the next frame row in the stream's order. CF=1 AX = BRI_DONE (the last) or
; BRI_STALE. Keeps every other register
bri_rowdone:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    call bri_gchk
    jc .stale
    cmp byte [bri_amode], 0
    je .box
    call bri_amrow                  ; an animation's frame: the master
    jmp .next
.box:
    mov ax, [bri_ft]
    add ax, [bri_fy]
    jc .next
    cmp ax, [bri_gh]
    jae .next                       ; off the screen
    push ax
    call bri_tyof
    mov [bri_ty], ax
    pop ax
    inc ax
    call bri_tyof
    mov [bri_ty1], ax
    cmp ax, [bri_ty]
    jbe .next                       ; shrunk: no box row is this one's
    call bri_trowfill
.ty:
    mov bx, [bri_ty]
    cmp bx, [bri_ty1]
    jae .next
    call bri_convrow
    inc word [bri_ty]
    jmp .ty
.next:
    inc word [bri_frow]
    mov ax, [bri_frow]
    cmp ax, [bri_fh]
    jae .done
    cmp byte [bri_ilace], 0
    jne .il
    inc word [bri_fy]
    jmp .go
.il:
    mov bl, [bri_pass]
    xor bh, bh
    mov al, [cs:bri_istep + bx]
    xor ah, ah
    add [bri_fy], ax
.pass:
    mov ax, [bri_fy]
    cmp ax, [bri_fh]
    jb .go
    inc byte [bri_pass]
    mov bl, [bri_pass]
    cmp bl, 4
    jae .done                       ; (every row counted: not reached)
    xor bh, bh
    mov al, [cs:bri_istart + bx]
    xor ah, ah
    mov [bri_fy], ax
    jmp .pass
.go:
    clc
    jmp .out
.done:
    mov word [bri_ret], BRI_DONE
    jmp .stop
.stale:
    mov word [bri_ret], BRI_STALE
.stop:
    stc
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    jnc .ret
    mov ax, [bri_ret]
.ret:
    ret
bri_istart: db 0, 4, 2, 1           ; the interlace passes (gif_frame_rows)
bri_istep:  db 8, 8, 4, 2

; bri_tyof - AX = a logical row -> AX = the first box row at or past it:
; ceil(AX th / H). DX clobbered
bri_tyof:
    mul word [bri_th]
    mov cx, [bri_gh]
    dec cx
    add ax, cx
    adc dx, 0
    inc cx
    div cx
    cmp ax, [bri_th]
    jbe .ret
    mov ax, [bri_th]
.ret:
    ret

; bri_trowfill - the box row's indices, through the column map: the frame
; row's, the background off the frame (or everywhere, [bri_allbg])
bri_trowfill:
    push ax
    push bx
    push cx
    push di
    push es
    mov es, [cs:PXP_SELF]
    mov di, bri_trow
    xor bx, bx                      ; BX = tx x 2
    mov cx, [bri_tw]
    cld
.l:
    mov al, [bri_bgi]
    cmp byte [bri_allbg], 0
    jne .st
    push bx
    mov bx, [cs:bri_cmap + bx]
    cmp bx, 0xFFFF
    je .bg
    mov al, [cs:bri_rowbuf + bx]
.bg:
    pop bx
.st:
    stosb
    inc bx
    inc bx
    loop .l
    pop es
    pop di
    pop cx
    pop bx
    pop ax
    ret

; bri_convrow - box row (border + BX) from the indices in bri_trow: a pixel
; is c2 when BAYER8[y & 7][x & 7] < t, else c1 (lit when < t1, in 1bpp), with
; x and y counted from the box's corner. All kept
bri_convrow:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov es, [bri_news]
    mov al, [bri_bord]
    xor ah, ah
    mov cx, ax                      ; CX = x, from the border
    add ax, bx                      ; AX = the box row
    mov si, ax
    mul word [bri_stride]
    mov di, ax                      ; ES:DI = the row
    and si, 7
    shl si, 1
    shl si, 1
    shl si, 1
    add si, px_bayer                ; CS:SI = its eight thresholds
    mov bp, [bri_tw]
    mov word [bri_tx], 0
.px:
    mov bx, [bri_tx]
    mov al, [cs:bri_trow + bx]
    xor ah, ah
    mov bx, cx
    and bx, 7
    mov dh, [cs:si + bx]            ; DH = the threshold here
    cmp byte [bri_mono], 0
    je .colour
    mov bx, ax
    xor dl, dl
    cmp dh, [cs:bri_t1 + bx]
    jae .put
    inc dl                          ; lit
    jmp .put
.colour:
    mov bx, ax
    shl bx, 1
    add bx, ax                      ; its plan
    mov dl, [cs:bri_ptab + bx]      ; c1...
    cmp dh, [cs:bri_ptab + bx + 2]
    jae .put
    mov dl, [cs:bri_ptab + bx + 1]  ; ...or c2
.put:
    mov bx, cx                      ; the byte and the bit
    shr bx, 1
    shr bx, 1
    shr bx, 1
    add bx, di
    push cx
    and cl, 7
    mov ah, 0x80
    shr ah, cl
    mov al, ah
    not al
    mov cx, 1
    cmp byte [bri_mono], 0
    jne .p
    mov cl, 4
.p:
    shr dl, 1
    jnc .clr
    or [es:bx], ah
    jmp .pn
.clr:
    and [es:bx], al
.pn:
    add bx, [bri_bc]
    loop .p
    pop cx
    inc cx
    inc word [bri_tx]
    dec bp
    jnz .px
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; bri_movedown - the new copy to the ready copies' end, where the staged file
; was: a forward copy, the destination below the source
bri_movedown:
    push ds
    push es
    mov cx, [bri_rbytes]
    mov es, [bri_stgs]
    mov dx, [bri_news]
    mov ds, dx
    xor si, si
    xor di, di
    shr cx, 1
    cld
    rep movsw
    adc cx, cx
    rep movsb
    pop es
    pop ds
    ret

%include "brimage_anim.inc"
%include "brimage_px.inc"

; =============================================================================
; SHARED SOURCE: PiXEL's plan search, its 1bpp threshold and the Bayer table
; (SPEC.md 106.11), assembled here as they are in PiXEL's parts
; =============================================================================
%include "pxplan1.inc"
%include "pxt1.inc"
%include "pxbayer.inc"

; --- the part's own tables: written through ES = [cs:PXP_SELF], read through CS
bri_rowbuf: times BRI_ROWMAX db 0   ; a frame row's indices
bri_cmap:   times BRI_TWMAX dw 0    ; each box column's frame column
bri_trow:   times BRI_TWMAX db 0    ; a box row's indices
bri_ptab:   times 256 * 3 db 0      ; the plans
bri_t1:     times 256 db 0          ; the 1bpp thresholds
bri_arec:   times BRI_AMAX * A_SIZE db 0  ; the animations (SPEC.md 71.29)
bri_amp:    times 256 dw 0          ; a frame's indices, mapped
bri_ix:     times BRI_IXSZ db 0     ; the page's index (SPEC.md 71.28)
bri_ixk:    times BR_IMGTMAX db 0   ; each image's slot, FFh none, FEh fetch
bri_lm:     times BRI_LMMAX db 0    ; the response's Last-Modified...
bri_etag:   times BRI_ETMAX db 0    ; ...and ETag

; --- PNG and JPEG (SPEC.md 71.30): the host's own words ...
bri_kind:   db 0                    ; BRK_*: the image in hand
bri_pxheld: db 0                    ; the decoder part held: 0, 2 ... 6
bri_fmt:    db 0                    ; PXF_*: the sniff's, for the record
bri_xfmt:   db 0                    ; PXF_*: what the URL's extension names
bri_pxcs:   dw 0                    ; ...its code segment
bri_pxds:   dw 0                    ; ...and its data one
bri_prim:   dw 0                    ; the Browser's DS, for the services
bri_pxfar:  dd 0                    ; a vector, far
bri_sw:     dw 0                    ; the source's size, upright
bri_sh:     dw 0
bri_rf:     db 0                    ; RF_*
bri_nxt:    db 0                    ; K_NEXT has handed the file over
bri_planed: db 0                    ; the plans are made
bri_pxdp:   dw 0                    ; the decoder's scratch, paragraphs
bri_pxrp:   dw 0                    ; the row buffer's
bri_rseg:   dw 0                    ; ...and its segment
bri_sx0:    db 0                    ; K_SCAT's first column...
bri_ssh:    db 0                    ; ...and its step's shift
bri_cr0:    dw 0                    ; K_COLS's block: its rows,
bri_cr1:    dw 0
bri_cc0:    dw 0                    ; its columns,
bri_cc1:    dw 0
bri_cbs:    dw 0                    ; where it is,
bri_cbo:    dw 0
bri_cbr:    dw 0                    ; and a row's bytes
bri_rgb:    db 0, 0, 0              ; a colour being planned

; ... and THE HOST BLOCK, the words a decoder reads through DS by these
; names (tools/brpxlink.py writes them out from part 1's map). Only these
; are read and written by a HEAD or a DECODE; the rest are PiXEL's PLANS
; vector's alone, which part 1 never calls, so they share one block
bri_pxhost:
px_k:       times PXK_SZ db 0       ; the context, DI on every call
px_cur:     times PXR_SZ db 0       ; (a PNG marks PXR_KEEP in it)
px_spal:    times 768 db 0          ; the source palette (PXK_SPAL)
px_fname:   times 16 db 0           ; (an in-memory file: never read)
px_rflat:   db 0                    ; 1: the file is in memory
px_abort:   db 0
px_onworker: db 0
px_hchan:   db 0
px_flatseg: dw 0                    ; the in-memory file: the stage
px_flatlen: dd 0
px_hseg:    dw 0
px_hcap:    dw 0
px_clsz:    dw 0
px_win:     dw 0
px_rpos:    dw 0                    ; the ring window (pxextra, pxsimp):
px_rend:    dw 0                    ; the file whole, one window
px_rsegc:   dw 0
px_rbase:   dd 0
px_sbase:   dd 0                    ; (a PNG inside an ICO: refused)
px_wseg:    dw 0                    ; pxsimp's work: the row buffer...
px_wslin:   dw 0                    ; ...a PCX line...
px_wslut:   dw 0                    ; ...and a KB of tables (SPEC.md 71.31)
px_hlen:    dw 0                    ; the head's bytes: the file's
px_hsz:     dw 0                    ; (pxsimp's own words from here on)
px_bgr:     db 0
px_dbpl:    dw 0
px_dbpp:    dw 0
px_dcomp:   dd 0
px_desz:    db 0
px_dfmt:    db 0
px_dkind:   db 0
px_dlinsz:  dw 0
px_dmask:   db 0
px_dmaxv:   dw 0
px_dnpl:    dw 0
px_doff:    dd 0
px_dpalo:   dw 0
px_dpsz:    db 0
px_drawsz:  dw 0
px_dstride: dw 0
px_dtop:    db 0
px_dused:   dd 0
px_mbo:     db 0, 0, 0
px_mbs:     db 0, 0, 0
px_mmx:     db 0, 0, 0
px_pvalid:  db 0
px_rcnt2:   db 0
px_ri:      dw 0
px_rnib:    db 0
px_rpix:    dd 0
px_rraw:    db 0
px_rsamp:   dw 0
px_rtri:    db 0, 0, 0
px_rval:    db 0
px_rx:      dw 0
px_th:      dd 0
px_tw:      dd 0
px_tkpos:   dw 0
px_tokeof:  dw 0
px_tokget:  dw 0
px_pal:
px_tbuf:                            ; (a PCX's 769-byte tail)
px_plan:
px_mwc:
px_n0:
px_n1:
px_n2:
px_dv0:
px_dv1:
px_hn:
px_hs1:
px_hmean:
px_hsd:
px_hmin:
px_hmax:    times 776 db 0          ; PLANS's alone (above)
bri_pxhend:
