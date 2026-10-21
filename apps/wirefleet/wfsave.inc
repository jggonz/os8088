; Versioned, checksummed private match. SPEC.md 114.7. UI-task I/O only.
wf_save:
    mov al, FDLG_SAVE
    jmp wf_filedialog
wf_load:
    mov al, FDLG_OPEN
wf_filedialog:
    mov bx, [wf_win]
    mov si, wf_defaultfile
    mov di, wf_filedone
    call OSAPI_FILE_DLG
    jnc .out
    mov word [wf_message], wf_dialogbusy
    jmp wf_update
.out:
    ret

wf_filedone:
    mov [wf_filemode], al
    mov [wf_filesizelo], cx
    mov [wf_filesizehi], dx
    mov si, di
    mov di, wf_filename
    mov cx, 12
.name:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    loop .name
    mov byte [di], 0
    cmp byte [wf_filemode], FDLG_SAVE
    jne .load
    call wf_snapshot
    push ds
    pop es
    mov si, wf_filename
    mov bx, wf_savebuf
    mov cx, 512
    xor dx, dx
    call OSAPI_FILE_WRITE
    jc .diskerror
    mov word [wf_message], wf_savedmessage
    jmp wf_update
.load:
    cmp word [wf_filesizehi], 0
    jne .bad
    cmp word [wf_filesizelo], 512
    jne .bad
    push ds
    pop es
    mov si, wf_filename
    mov bx, wf_savebuf
    mov cx, 512
    xor dx, dx
    call OSAPI_FILE_READ
    jc .diskerror
    or dx, dx
    jnz .bad
    cmp ax, 512
    jne .bad
    call wf_snapshot_check
    jc .bad
    call wf_disconnect
    call wf_fx_cancel           ; 114.11: no flight outlives the old match
    call wf_soundquiet
    push ds
    pop es
    mov bx, wf_savefields
.restore:
    mov di, [bx]
    or di, di
    jz .restored
    mov si, [bx+2]
    add si, wf_savebuf
    mov cx, [bx+4]
    rep movsb
    add bx, 6
    jmp .restore
.restored:
    mov word [wf_buttons+OS88UI_BT_N], 5
    mov byte [wf_chatlog], 0
    mov byte [wf_chatlog+40], 0
    mov byte [wf_chatlog+80], 0
    mov byte [wf_chatline], 0
    mov byte [wf_input], 0
    mov byte [wf_line+LN_FOCUS], 0
    mov word [wf_line+LN_LEN], 0
    mov word [wf_line+LN_CAR], 0
    mov byte [wf_connected], 0
    mov byte [wf_conflict], 0
    mov byte [wf_linkopen], 0
    mov byte [wf_lastout], 0xff
    call wf_autostart           ; the saved port first (114.9)
    mov byte [wf_chatmode], 0
    mov byte [wf_abon], 0
    mov byte [wf_rlen], 0
    mov byte [wf_rescape], 0
    mov word [wf_peerturn], 0xffff
    mov word [wf_txlen], 0
    mov word [wf_txpos], 0
    mov byte [wf_urgent], 1
    mov word [wf_message], wf_loadedmessage
    jmp wf_repaint
.bad:
    mov word [wf_message], wf_badmatch
    jmp wf_update
.diskerror:
    mov word [wf_message], wf_diskerror
    jmp wf_update

wf_snapshot:
    push ds
    pop es
    mov di, wf_savebuf
    mov cx, 256
    xor ax, ax
    rep stosw
    mov word [wf_savebuf], 'WF'
    mov word [wf_savebuf+2], 'LT'
    mov byte [wf_savebuf+4], 1
    mov word [wf_savebuf+6], 512
    mov bx, wf_savefields
.field:
    mov si, [bx]
    or si, si
    jz .crc
    mov di, [bx+2]
    add di, wf_savebuf
    mov cx, [bx+4]
    rep movsb
    add bx, 6
    jmp .field
.crc:
    mov si, wf_savebuf
    mov cx, 510
    mov ax, 0xffff
    call wf_crc
    mov [wf_savebuf+510], ax
    ret

wf_snapshot_check:
    cmp word [wf_savebuf], 'WF'
    jne .bad
    cmp word [wf_savebuf+2], 'LT'
    jne .bad
    cmp word [wf_savebuf+4], 1
    jne .bad
    cmp word [wf_savebuf+6], 512
    jne .bad
    mov si, wf_savebuf
    mov cx, 510
    mov ax, 0xffff
    call wf_crc
    cmp ax, [wf_savebuf+510]
    jne .bad
    mov si, wf_savebuf+393
    mov cx, 117
.reserved:
    lodsb
    or al, al
    jnz .bad
    loop .reserved
    mov bx, wf_savebounds
.bound:
    mov si, [bx]
    or si, si
    jz .identity
    mov al, [wf_savebuf+si]
    cmp al, [bx+2]
    ja .bad
    add bx, 3
    jmp .bound
.identity:
    mov ax, [wf_savebuf+16]
    or ax, [wf_savebuf+18]
    jz .bad
    cmp word [wf_savebuf+8], 200
    ja .bad
    test byte [wf_savebuf+31], 0xe0
    jnz .bad
    mov ax, [wf_savebuf+20]
    or ax, [wf_savebuf+22]
    jz .unpaired
    mov ax, [wf_savebuf+18]
    cmp ax, [wf_savebuf+22]
    jb .host
    ja .guest
    mov ax, [wf_savebuf+16]
    cmp ax, [wf_savebuf+20]
    je .bad
    jb .host
.guest:
    cmp byte [wf_savebuf+12], 1
    jne .bad
    mov si, wf_savebuf+20
    jmp .match
.host:
    cmp byte [wf_savebuf+12], 0
    jne .bad
    mov si, wf_savebuf+16
.match:
    mov di, wf_savebuf+24
    call wf_equal4
    jne .bad
    jmp .fleet
.unpaired:
    cmp byte [wf_savebuf+28], 0
    jne .bad
    cmp word [wf_savebuf+8], 0
    jne .bad
    mov ax, [wf_savebuf+24]
    or ax, [wf_savebuf+26]
    jnz .bad
.fleet:
    push ds
    pop es
    mov di, wf_checkboard
    xor ax, ax
    mov cx, 100
    rep stosb
    mov byte [wf_checkplaced], 0
    mov byte [wf_checkship], 0
.ship:
    xor bx, bx
    mov bl, [wf_checkship]
    mov al, [wf_savebuf+49+bx]
    cmp al, 1
    ja .bad
    mov al, [wf_savebuf+44+bx]
    cmp al, 255
    je .nextship
    cmp al, 100
    jae .bad
    xor ah, ah
    mov dl, 10
    div dl
    mov cl, [wf_lengths+bx]
    xor ch, ch
    mov dx, 1
    cmp byte [wf_savebuf+49+bx], 0
    je .edge
    mov ah, al
    mov dx, 10
.edge:
    add ah, cl
    cmp ah, 10
    ja .bad
    mov al, [wf_savebuf+44+bx]
    xor ah, ah
    mov di, ax
    mov al, bl
    inc al
.place:
    cmp byte [wf_checkboard+di], 0
    jne .bad
    mov [wf_checkboard+di], al
    add di, dx
    loop .place
    inc byte [wf_checkplaced]
.nextship:
    inc byte [wf_checkship]
    cmp byte [wf_checkship], 5
    jb .ship
    mov al, [wf_checkplaced]
    cmp al, [wf_savebuf+42]
    jne .bad
    cmp byte [wf_savebuf+13], 0
    je .compare
    cmp al, 5
    jne .bad
.compare:
    mov si, wf_checkboard
    mov di, wf_savebuf+54
    mov cx, 100
    repe cmpsb
    jne .bad
    xor bx, bx
    xor cx, cx                  ; CL own shots, CH incoming shots
    xor dx, dx                  ; DL outgoing hits, DH incoming hits
.shots:
    mov al, [wf_savebuf+254+bx]
    cmp al, 3
    ja .bad
    or al, al
    jz .incoming
    inc cl
    cmp al, 2
    jb .incoming
    inc dl
.incoming:
    mov al, [wf_savebuf+154+bx]
    cmp al, 3
    ja .bad
    or al, al
    jz .nextcell
    inc ch
    cmp al, 1
    jne .hit
    cmp byte [wf_checkboard+bx], 0
    jne .bad
    jmp .nextcell
.hit:
    cmp byte [wf_checkboard+bx], 0
    je .bad
    inc dh
.nextcell:
    inc bx
    cmp bx, 100
    jb .shots
    call wf_snapshot_sunkcheck
    jc .bad
    cmp dl, 17
    ja .bad
    cmp dh, 17
    ja .bad
    cmp dl, 17
    jne .notwon
    cmp byte [wf_savebuf+15], 1
    jne .bad
.notwon:
    cmp dh, 17
    jne .notlost
    cmp byte [wf_savebuf+15], 2
    jne .bad
.notlost:
    cmp byte [wf_savebuf+15], 1
    jne .lostflag
    cmp dl, 17
    jne .bad
.lostflag:
    cmp byte [wf_savebuf+15], 2
    jne .counts
    cmp dh, 17
    jne .bad
.counts:
    mov ax, [wf_savebuf+8]
    or ax, ax
    jz .turns
    cmp byte [wf_savebuf+13], 1
    jne .bad
    cmp byte [wf_savebuf+14], 1
    jne .bad
.turns:
    shr ax, 1
    mov bx, ax
    test byte [wf_savebuf+8], 1
    jz .outcount
    cmp byte [wf_savebuf+12], 0
    jne .incomingextra
    inc al
    jmp .outcount
.incomingextra:
    inc bl
.outcount:
    cmp cl, al
    jne .bad
    cmp ch, bl
    jne .bad
    cmp byte [wf_savebuf+28], 0
    je .cache
    cmp byte [wf_savebuf+13], 1
    jne .bad
    cmp byte [wf_savebuf+14], 1
    jne .bad
    cmp byte [wf_savebuf+15], 0
    jne .bad
    mov al, [wf_savebuf+8]
    and al, 1
    cmp al, [wf_savebuf+12]
    jne .bad
    xor bx, bx
    mov bl, [wf_savebuf+29]
    cmp byte [wf_savebuf+254+bx], 0
    jne .bad
.cache:
    cmp byte [wf_savebuf+30], 0
    je .chat
    mov ax, [wf_savebuf+8]
    sub ax, [wf_savebuf+32]
    cmp ax, 1
    jb .bad
    cmp ax, 2
    ja .bad
    mov al, [wf_savebuf+32]
    and al, 1
    cmp al, [wf_savebuf+12]
    je .bad
    mov al, [wf_savebuf+34]
    cmp al, 100
    jae .bad
    xor ah, ah
    mov bx, ax
    mov al, [wf_savebuf+35]
    cmp al, 1
    jb .bad
    cmp al, 3
    ja .bad
    cmp al, [wf_savebuf+154+bx]
    jne .bad
    cmp al, 3
    jne .nosunkfields
    mov al, [wf_savebuf+36]
    cmp al, 1
    jb .bad
    cmp al, 5
    ja .bad
    cmp al, [wf_savebuf+54+bx]
    jne .bad
    dec al
    xor ah, ah
    mov si, ax
    mov al, [wf_savebuf+44+si]
    cmp al, [wf_savebuf+37]
    jne .bad
    mov al, [wf_savebuf+49+si]
    cmp al, [wf_savebuf+38]
    jne .bad
    jmp .chat
.nosunkfields:
    mov al, [wf_savebuf+36]
    or al, [wf_savebuf+37]
    or al, [wf_savebuf+38]
    jnz .bad
.chat:
    cmp byte [wf_savebuf+358], 0
    je .ok
    mov al, [wf_savebuf+359]
    or al, al
    jz .bad
    xor ah, ah
    mov cx, ax
    mov si, wf_savebuf+360
.ascii:
    lodsb
    cmp al, 32
    jb .bad
    cmp al, 126
    ja .bad
    loop .ascii
.ok:
    clc
    ret
.bad:
    stc
    ret

wf_snapshot_sunkcheck:
    push ax
    push bx
    push cx
    push dx
    push si
    xor si, si
    xor bx, bx
.count:
    cmp byte [wf_savebuf+254+bx], 3
    jne .next
    inc si
.next:
    inc bx
    cmp bx, 100
    jb .count
    xor ax, ax
    xor bx, bx
    mov dl, [wf_savebuf+31]
.mask:
    test dl, 1
    jz .nextmask
    add al, [wf_lengths+bx]
.nextmask:
    shr dl, 1
    inc bx
    cmp bx, 5
    jb .mask
    cmp ax, si
    jne .bad
    xor si, si
.ship:
    mov cx, si
    inc cl
    xor dx, dx
    xor bx, bx
.cell:
    cmp [wf_savebuf+54+bx], cl
    jne .nextcell
    cmp byte [wf_savebuf+154+bx], 2
    jb .nextcell
    inc dl
    cmp byte [wf_savebuf+154+bx], 3
    jne .nextcell
    inc dh
.nextcell:
    inc bx
    cmp bx, 100
    jb .cell
    cmp dl, [wf_lengths+si]
    je .all
    or dh, dh
    jnz .bad
    jmp .nextship
.all:
    cmp dh, dl
    jne .bad
.nextship:
    inc si
    cmp si, 5
    jb .ship
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

wf_savefields:
    dw wf_turn,8,2, wf_hash,10,2, wf_side,12,1, wf_ready,13,1
    dw wf_peerready,14,1, wf_finished,15,1, wf_identity,16,12
    dw wf_pending,28,1, wf_pendingcell,29,1, wf_cachevalid,30,1
    dw wf_enemysunk,31,1, wf_cacheturn,32,2, wf_cachecell,34,5
    dw wf_selected,39,1, wf_direction,40,1, wf_target,41,1
    dw wf_placed,42,1, wf_muted,43,1, wf_anchors,44,5, wf_dirs,49,5
    dw wf_board,54,300, wf_chatseq,354,2, wf_chatrxseq,356,2
    dw wf_chatpending,358,1, wf_chatlength,359,1, wf_chattext,360,32
    dw wf_wantport,392,1, 0,0,0
wf_savebounds:
    dw 12
    db 1
    dw 13
    db 1
    dw 14
    db 1
    dw 15
    db 2
    dw 28
    db 1
    dw 29
    db 99
    dw 30
    db 1
    dw 39
    db 4
    dw 40
    db 1
    dw 41
    db 99
    dw 42
    db 5
    dw 43
    db 1
    dw 358
    db 1
    dw 359
    db 32
    dw 392
    db 2
    dw 0
wf_defaultfile: db 'FLEET.WFL',0
wf_savedmessage: db 'Match saved. Save the other captain on the other PC too.',0
wf_loadedmessage: db 'Saved match loaded, paused until the same opponent answers.',0
wf_badmatch: db 'Invalid saved match. Your current fleet and turn were kept.',0
wf_diskerror: db 'Could not read or write the match file.',0
wf_dialogbusy: db 'File dialog unavailable. Finish the open dialog and try again.',0
