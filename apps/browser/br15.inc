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
; The file's image is in the URL store at BR_URLBM (SPEC.md 71.23), so these
; are OFFSETS there, read through [br_urlseg]; the menu reads br_bmtitles
BR_BMCOUNT     equ BR_URLBM + 4
BR_BMURLBASE   equ BR_URLBM + BR_BMHEADER + BR_BMTITLE
BR_BMREC       equ BR_URLBM + BR_BMHEADER
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
    test byte [br_parse_started], BR_PARSE_LAYOUT
    jnz .busy                       ; as br_onclick and br_onkey: the click on
    mov [br_ctxpx], cx              ; its item would be dropped, and the
    mov [br_ctxpy], dx              ; layout's last paint covers it while it
    jmp br_ctxshow                  ; stays open (SPEC.md 71.24)
.busy:
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
    call br_urlneed                 ; the file's image lives in the URL store
    jc .nostore                     ; (SPEC.md 71.23)
    mov es, [br_urlseg]
    mov bx, BR_URLBM
    mov cx, BR_BMFILEBYTES
    xor dx, dx
    mov si, br_s_bmfile
    xor ax, ax
    call br_pagepin                 ; ES:BX is a movable claim: pinned for the
    call od_read                    ; call (SPEC.md 66.3 rule 5)
    call br_pageunpin               ; AX and the flags kept
    jnc .got
    cmp ax, FERR_NOENT              ; a missing file is the only honest empty
    je .empty
    jmp .bad                        ; any other read failure is not "empty"
.got:
    or dx, dx
    jnz .bad
    cmp ax, BR_BMHEADER
    jb .bad
    cmp byte [es:BR_URLBM], 'B'
    jne .bad
    cmp byte [es:BR_URLBM+1], 'M'
    jne .bad
    cmp byte [es:BR_URLBM+2], '0'
    jne .bad
    cmp byte [es:BR_URLBM+3], '1'
    jne .bad
    cmp byte [es:BR_BMCOUNT], BR_BMMAX
    ja .bad
    xor bx, bx
    mov [br_copycap], ax
    mov bl, [es:BR_BMCOUNT]
    mov ax, BR_BMRECSZ
    mul bx
    add ax, BR_BMHEADER
    cmp ax, [br_copycap]
    ja .bad
    mov cl, [es:BR_BMCOUNT]         ; a record's text fields end in NUL whatever
    xor ch, ch                      ; the file held
    mov bx, BR_BMREC
    jcxz .okend
.nul:
    mov byte [es:bx+BR_BMTITLE-1], 0
    mov byte [es:bx+BR_BMRECSZ-1], 0
    add bx, BR_BMRECSZ
    loop .nul
.okend:
    clc
    jmp .done
.nostore:                           ; no store, so no cache: the menu is
    stc                             ; empty and nothing may be written
    jmp short .done
.bad:
    stc                             ; CF=1: read failed or foreign file; the
    jmp short .clr                  ; cache is empty but must not be written
.empty:
    clc
.clr:
    pushf                           ; ES is the store on both ways in
    mov di, BR_URLBM
    xor ax, ax
    mov cx, (BR_BMFILEBYTES+1)/2
    cld
    rep stosw
    mov byte [es:BR_URLBM], 'B'
    mov byte [es:BR_URLBM+1], 'M'
    mov byte [es:BR_URLBM+2], '0'
    mov byte [es:BR_URLBM+3], '1'
    popf
.done:
    pushf
    call br_bmmenuset
    popf
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; The Bookmarks menu from the store: each record's title is copied into
; br_bmtitles, which the menu points at, because a menu item is a pointer into
; OUR segment. Called after every change to the records.
br_bmmenuset:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cld
    xor cx, cx
    cmp word [br_urlseg], 0
    je .none
    mov es, [br_urlseg]
    mov cl, [es:BR_BMCOUNT]         ; count is a byte; the next byte is title text
    jcxz .none
    xor bx, bx
.item:
    mov ax, BR_BMRECSZ
    mul bx                          ; DX written, and kept for the caller
    add ax, BR_BMREC
    mov si, ax                      ; the record's title, in the store
    mov ax, BR_BMTITLE
    mul bx
    add ax, br_bmtitles
    mov di, ax                      ; ...and the menu's copy of it
    mov dx, ax
    push cx
    push ds
    push ds
    push es
    pop ds                          ; DS = the store
    pop es                          ; ES = ours
    mov cx, BR_BMTITLE
    rep movsb
    pop ds
    mov es, [br_urlseg]
    pop cx
    mov di, bx
    shl di, 1
    mov [br_bmitems+di], dx
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
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; AL = Bookmarks menu item -> its URL copied into br_ubuf. CF=1 = no such
; record (the `(empty)` label, or no store). Every register kept.
br_bmurlget:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov dx, [br_urlseg]
    or dx, dx
    jz .no
    mov es, dx
    cmp al, [es:BR_BMCOUNT]
    jae .no
    mov bl, al
    xor bh, bh
    mov ax, BR_BMRECSZ
    mul bx                          ; **MUL WRITES DX**: the store's segment
    mov dx, es                      ; comes back from ES
    add ax, BR_BMURLBASE
    mov si, ax
    push ds
    pop es                          ; ES = ours, for br_ubuf
    mov di, br_ubuf
    mov cx, BR_UBUF
    push ds
    mov ds, dx                      ; DS = the store, for the record
    call br_copyz
    pop ds
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

; Search DS:SI's canonical URL. CF=0 and AX=index when found.
br_bmfind:
    push si
    mov si, br_ubuf
    call br_bmfindurl
    pop si
    ret

; The bookmark worker searches its queued URL directly, without temporarily
; replacing the live location field in br_ubuf.
br_bmfindurl:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    xor bx, bx
    cmp word [br_urlseg], 0         ; no store, no records
    je .miss
    mov es, [br_urlseg]             ; the records are the store's, the query
    mov cx, si                      ; at DS:SI ours
.next:
    cmp bl, [es:BR_BMCOUNT]         ; count is a byte; mul below clobbers DX
    jae .miss
    mov si, cx                      ; EVERY record from the query's start: a
                                    ; mismatch at byte k left SI at k, and the
                                    ; next record was compared with the query's
                                    ; tail, so only record 0 could be found
    mov ax, bx
    mov di, BR_BMRECSZ
    mul di
    add ax, BR_BMURLBASE
    mov di, ax
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
    ; Disk APIs are foreign to Browser URL state. Keep the active origin around
    ; the asynchronous bookmark transaction so the page's relative links remain
    ; resolvable after its bookmark has been written.
    push ds
    pop es
    mov si, br_host
    mov di, br_bmworkhost
    mov cx, BR_HOSTMAX
    cld
    rep movsb
    mov ax, [br_port]
    mov [br_bmworkport], ax
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
    jnc .readok
    mov byte [br_bmreq], 0          ; unreadable or foreign file: never write
    or al, al                       ; over it, and say so
    jz .refresh_only
    jmp .failmsg
.readok:
    or al, al
    jz .refresh_only
    mov byte [br_bmreq], 0
    push ds
    pop es
    mov si, br_bmrequrl
    call br_bmfindurl
    mov byte [br_bmsaved], 0
    jc .not_saved
    mov byte [br_bmsaved], 1
.not_saved:
    cmp byte [br_bmopwork], 4
    jne .decide
    cmp byte [br_bmsaved], 0
    je .toggle_add
    mov byte [br_bmopwork], 2
    jmp .decide
.toggle_add:
    mov byte [br_bmopwork], 1
.decide:
    call br_bmcommit                ; add or remove, and write: part 0's
    jc .failed                      ; (SPEC.md 71.24), so a part that cannot
.success:                           ; be read is a failed save, said as one
    mov byte [br_nstate], BN_DONE
    jmp .restore
.failed:
    call br_bmrefresh
.failmsg:
    mov word [br_nmsg], br_s_bmfail
    mov byte [br_nstate], BN_DONE
.restore:
.refresh_only:
    call br_bmcheck
    call br_bmmenuset
    mov byte [br_bmredraw], 1      ; repaint after the disk operation returns
.out:
    push ds
    pop es
    mov si, br_bmworkhost
    mov di, br_host
    mov cx, BR_HOSTMAX
    cld
    rep movsb
    mov ax, [br_bmworkport]
    mov [br_port], ax
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Copy a NUL-terminated string DS:SI -> ES:DI into a field of CX bytes: at most
; CX-1 characters, always a NUL, the rest zero-filled.
br_copyz:
    push ax
    push cx
    push di
    push si
    cld
    jcxz .done
    dec cx
.copy:
    jcxz .term
    lodsb
    or al, al
    jz .term
    stosb
    dec cx
    jmp .copy
.term:
    xor al, al
    stosb
    rep stosb
.done:
    pop si
    pop di
    pop cx
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
