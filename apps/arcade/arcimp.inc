; =============================================================================
; os8088 - apps/arcade/arcimp.inc - ROM SETS, IMPORTED ON THE MACHINE
; (ARCADE-SPEC 1.5; docs/plans/ARCADE-RELEASE-PLAN.md 2.2). Included by
; arcade.asm.
;
; Game > Add ROM sets... opens the file chooser. The user picks a MAME .ZIP;
; it is read whole, its central directory is matched against every set
; ARCADE knows (arcsets.inc: the chips' names, places, sizes and CRC-32s,
; generated from tools/getarcrom.py's pins), and each set found COMPLETE and
; RIGHT - every chip there, every chip's CRC-32 the pinned one - is unpacked
; (stored, or inflated by apps/os88inflate.inc), each chip checked again as
; it comes out, and written as its <SET>.ROM IN THE FOLDER THE ZIP IS IN.
; That folder is remembered in ARCADE.DAT (SYSTEM\APPDATA on ARCADE's own
; disk), and a pick looks there after its own (ar_romload). One zip may hold
; many sets: MAME's invaders.zip carries some forty clones in folders, and
; only names at its root are looked at. Nothing in the zip is trusted: every
; length and offset is checked against the file before it is used.
; =============================================================================

AR_ZMAXKB   equ 448                 ; a zip bigger than this is refused
AR_NPLACE   equ 8                   ; folders remembered

; ar_import - Game > Add ROM sets...
ar_import:
    mov al, FDLG_OPEN
    mov bx, [ar_win]
    mov di, ar_impdone
    xor si, si
    call OSAPI_FILE_DLG
    jnc .r
    mov si, ar_s_nodlg
    call ar_toast
.r:
    ret

; ar_impdone - the chooser's answer (SPEC.md 38.6): ES:DI = the name, DX:CX
; = its size. The board picked is let go for the memory, and picked again
; after
ar_impdone:
    mov [ar_zsize], cx
    mov [ar_zsize+2], dx
    mov si, di                      ; the name, out of the kernel's buffer
    mov di, ar_impname
    mov cx, 13
.cp:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    loop .cp
    mov byte [ar_impname+12], 0
    push ds
    pop es
    mov si, ar_impname              ; a .ZIP
.dot:
    lodsb
    or al, al
    jz .notzip
    cmp al, '.'
    jne .dot
    lodsw
    or ax, 0x2020
    cmp ax, 'zi'
    jne .notzip
    lodsw
    or al, 0x20
    cmp ax, 'p'
    jne .notzip
    mov ax, [ar_zsize]              ; the size, in KB, and two over
    mov dx, [ar_zsize+2]
    or ax, dx
    jz .notzip
    mov ax, [ar_zsize]
    add ax, 1023
    adc dx, 0
    mov cl, 10
    shr ax, cl
    mov cl, 6
    shl dx, cl
    or ax, dx
    add ax, 2
    cmp ax, AR_ZMAXKB
    ja .big
    mov [ar_zkb], ax
    mov bx, [ar_bd]                 ; room: the board picked, let go
    mov [ar_impbd], bx
    call ar_unselect
    mov ax, [ar_zkb]
    call OSAPI_MEM_CLAIM
    jc .nomem
    mov [ar_zclaim], dx
    mov es, dx
    xor bx, bx
    mov ax, [ar_zkb]
    mov dx, ax
    mov cl, 6
    shr dx, cl
    mov cl, 10
    shl ax, cl
    mov cx, ax
    mov si, ar_impname
    call OSAPI_FILE_READ
    jc .unread
    mov [ar_zsize], ax              ; what was read
    mov [ar_zsize+2], dx
    call ar_zeocd
    jc .unread
    mov byte [ar_nimp], 0
    mov byte [ar_nwrong], 0
    mov si, ar_sets
.set:
    lodsw
    or ax, ax
    jz .sets
    push si
    mov si, ax
    call ar_impset
    pop si
    jmp .set
.sets:
    call ar_zfree
    cmp byte [ar_nimp], 0
    je .none
    call ar_remember                ; this folder, for the picks
    mov si, ar_s_added
    mov al, [ar_nimp]
    add al, '0'
    mov [ar_s_addn], al
    jmp short .say
.none:
    mov si, ar_s_noset
    cmp byte [ar_nwrong], 0
    je .say
    mov si, ar_s_wrongz
    jmp short .say
.unread:
    call ar_zfree
    mov si, ar_s_badzip
    jmp short .say
.nomem:
    mov si, ar_s_nomem
    jmp short .say
.big:
    mov si, ar_s_bigzip
    jmp short .say2
.notzip:
    mov si, ar_s_notzip
    jmp short .say2
.say:
    call ar_toast
    call ar_gohome                  ; (the chooser left us in the zip's folder)
    mov bx, [ar_impbd]              ; the board as it was - or, if it was
    or bx, bx                       ;   none, the first a set now makes
    jnz .back                       ;   possible
    mov byte [ar_qsel], 1
    mov si, ar_boards
.first:
    lodsw
    or ax, ax
    jz .picked
    push si
    mov bx, ax
    call ar_select
    pop si
    jc .first
.picked:
    mov byte [ar_qsel], 0
    jmp short .shown
.back:
    call ar_select
.shown:
    mov si, [ar_win]
    jmp ar_repaint
.say2:
    call ar_toast
    call ar_gohome
    mov si, [ar_win]
    jmp ar_repaint

; ar_gohome - back to the folder ARCADE started in: the chooser moves the
; instance to the file it opens, and ARCADE.O88's parts are read by name in
; the folder it stands in (MEASURED: "Cannot read my parts" at the pick after
; an import from a folder of its own)
ar_gohome:
    mov dx, [ar_homecl]
    mov bl, [ar_homedrv]
    call OSAPI_FILE_GOTO_QM
    ret

; ar_zfree - the zip's claim given back
ar_zfree:
    xor dx, dx
    xchg dx, [ar_zclaim]
    or dx, dx
    jz .r
    call OSAPI_MEM_FREE
.r:
    ret

; ar_zat - DX:AX = an offset into the zip: ES:BX = it, normalised (BX < 16).
; CF=1 when it is at or past the zip's end. Kept: all else
ar_zat:
    cmp dx, [ar_zsize+2]
    ja .out
    jb .in
    cmp ax, [ar_zsize]
    jae .out
.in:
    push ax
    push dx
    push cx
    mov bx, ax
    and bx, 15
    mov cl, 4
    shr ax, cl
    mov cl, 12
    shl dx, cl
    or ax, dx
    add ax, [ar_zclaim]
    mov es, ax
    pop cx
    pop dx
    pop ax
    clc
    ret
.out:
    stc
    ret

; ar_zeocd - the END OF CENTRAL DIRECTORY, looked for back from the end over
; a comment of up to 64KB (TorrentZip leaves one); out CF=0 [ar_cdseg]:0 =
; the central directory, normalised and wholly inside the zip and one
; segment, [ar_cdn] its entries
ar_zeocd:
    mov ax, [ar_zsize]
    mov dx, [ar_zsize+2]
    sub ax, 22
    sbb dx, 0
    jc .bad
    mov cx, 0xFFFF                  ; the comment's longest
.back:
    call ar_zat
    jc .bad
    cmp word [es:bx], 'PK'
    jne .not
    cmp word [es:bx+2], 0x0605
    je .found
.not:
    sub ax, 1
    sbb dx, 0
    jc .bad
    loop .back
.bad:
    stc
    ret
.found:
    mov cx, [es:bx+10]              ; the entries
    mov [ar_cdn], cx
    mov cx, [es:bx+12]              ; its size: within one segment
    cmp word [es:bx+14], 0
    jne .bad
    cmp cx, 0xFFF0
    ja .bad
    mov ax, [es:bx+16]              ; its offset, and its end in the zip
    mov dx, [es:bx+18]
    push ax
    push dx
    add ax, cx
    adc dx, 0
    cmp dx, [ar_zsize+2]
    ja .badp
    jb .fits
    cmp ax, [ar_zsize]
    ja .badp
.fits:
    pop dx
    pop ax
    call ar_zat
    jc .bad
    mov [ar_cdoff], bx
    mov [ar_cdseg], es
    mov [ar_cdend], cx
    add [ar_cdend], bx
    clc
    ret
.badp:
    pop dx
    pop ax
    stc
    ret

; ar_zfind - DS:SI = a chip's NUL name: ES:BX = its central directory entry
; (at the zip's root, the name's case ignored). CF=1 none. Kept: SI
ar_zfind:
    push cx
    push dx
    push di
    mov es, [ar_cdseg]
    mov bx, [ar_cdoff]
    mov cx, [ar_cdn]
.e:
    jcxz .none
    mov ax, bx                      ; the entry and its name inside the
    add ax, 46                      ;   directory
    jc .none
    cmp ax, [ar_cdend]
    ja .none
    cmp word [es:bx], 'PK'
    jne .none
    cmp word [es:bx+2], 0x0201
    jne .none
    mov dx, [es:bx+28]              ; the name's length
    lea di, [bx+46]
    push si
.ch:
    lodsb
    or al, al
    jz .end
    or dx, dx
    jz .no
    mov ah, [es:di]
    or ax, 0x2020                   ; (letters to lower case; digits and
    cmp al, ah                      ;   '.', '-' are unmoved by it)
    jne .no
    inc di
    dec dx
    jmp .ch
.end:
    or dx, dx                       ; the whole name, and nothing after
    jnz .no
    pop si
    clc
    jmp short .out
.no:
    pop si
    mov ax, [es:bx+28]              ; the next: 46 + name + extra + comment
    add ax, [es:bx+30]
    add ax, [es:bx+32]
    add ax, 46
    add bx, ax
    jc .none
    dec cx
    jmp .e
.none:
    stc
.out:
    pop di
    pop dx
    pop cx
    ret

; ar_impset - SI -> a set (arcsets.inc). Every chip found, sized and CRC'd
; in the directory first; then the image made and written. Counts
; [ar_nimp], or [ar_nwrong] for a set whose chips are there but not right
AS_NAME     equ 0                   ; a set: its .ROM's name (13)
AS_LEN      equ 13                  ;   the image's length (dword)
AS_N        equ 17                  ;   its chips
AS_CHIPS    equ 18                  ;   ...each:
AC_NAME     equ 0                   ;   its name (13)
AC_AT       equ 13                  ;   its place and size in the image
AC_LEN      equ 15
AC_CRC      equ 17                  ;   its CRC-32
AC_SIZE     equ 21
ar_impset:
    mov [ar_iset], si
    mov cl, [si+AS_N]
    xor ch, ch
    lea si, [si+AS_CHIPS]
    xor di, di                      ; DI = chips found right
    mov byte [ar_ifound], 0
.chip:
    push cx
    call ar_zfind
    jc .missing
    inc byte [ar_ifound]
    mov ax, [es:bx+24]              ; its size and CRC as the directory says
    cmp ax, [si+AC_LEN]
    jne .wrong
    cmp word [es:bx+26], 0
    jne .wrong
    mov ax, [es:bx+16]
    cmp ax, [si+AC_CRC]
    jne .wrong
    mov ax, [es:bx+18]
    cmp ax, [si+AC_CRC+2]
    jne .wrong
    mov ax, [es:bx+10]              ; stored or deflated, nothing else
    cmp ax, 8
    je .ok
    or ax, ax
    jnz .wrong
.ok:
    inc di
.wrong:
.missing:
    pop cx
    add si, AC_SIZE
    loop .chip
    mov si, [ar_iset]
    mov al, [si+AS_N]
    xor ah, ah
    cmp di, ax
    je .make
    cmp byte [ar_ifound], 0         ; some there and not right: wrong; none
    je .r                           ;   there: just not this set
    inc byte [ar_nwrong]
.r:
    ret
.make:
    mov ax, [si+AS_LEN]             ; the image's claim: header + image
    mov dx, [si+AS_LEN+2]
    add ax, 32 + 1023
    adc dx, 0
    mov cl, 10
    shr ax, cl
    mov cl, 6
    shl dx, cl
    or ax, dx
    call OSAPI_MEM_CLAIM
    jc .r
    mov [ar_iclaim], dx
    mov es, dx                      ; from nothing
    xor di, di
    mov cx, [si+AS_LEN]
    mov ax, [si+AS_LEN+2]
    add cx, 32
    adc ax, 0
    shr ax, 1
    rcr cx, 1                       ; (words; 64KB + 32 is 32,784)
    xor ax, ax
    cld
    rep stosw
    mov cl, [si+AS_N]
    xor ch, ch
    lea si, [si+AS_CHIPS]
.unpack:
    push cx
    call ar_zfind                   ; its local header, and its data
    jc .fail
    mov ax, [es:bx+42]
    mov dx, [es:bx+44]
    mov cx, [es:bx+10]
    mov [ar_imeth], cx
    mov cx, [es:bx+20]              ; the packed size, for the bounds
    mov [ar_ipack], cx
    cmp word [es:bx+22], 0
    jne .fail
    call ar_zat
    jc .fail
    cmp word [es:bx], 'PK'
    jne .fail
    cmp word [es:bx+2], 0x0403
    jne .fail
    mov cx, [es:bx+26]
    add cx, [es:bx+28]
    add cx, 30
    add ax, cx
    adc dx, 0
    push ax                         ; the data, and its end, in the zip
    push dx
    add ax, [ar_ipack]
    adc dx, 0
    cmp dx, [ar_zsize+2]
    ja .endbad
    jb .endok
    cmp ax, [ar_zsize]
    ja .endbad
.endok:
    pop dx
    pop ax
    call ar_zat
    jc .fail
    push si
    mov di, [si+AC_AT]              ; ES:DI = its place in the image
    mov cx, [si+AC_LEN]
    push es
    pop ds                          ; DS:SI = the data
    mov si, bx
    mov ax, [cs:ar_iclaim]
    add ax, 2
    mov es, ax
    cmp word [cs:ar_imeth], 8
    je .inf
    rep movsb
    clc
    jmp short .made
.inf:
    call inflate
.made:
    push cs
    pop ds
    pop si
    jc .fail
    push si                         ; the chip, as it came out
    mov cx, [si+AC_LEN]
    mov di, [si+AC_CRC]
    mov bp, [si+AC_CRC+2]
    mov si, [si+AC_AT]
    push ds
    mov ax, [ar_iclaim]             ; the image: the claim past its header
    add ax, 2
    mov ds, ax
    call ar_crc32
    pop ds
    pop si
    cmp ax, di
    jne .fail
    cmp dx, bp
    jne .fail
    pop cx
    add si, AC_SIZE
    dec cx
    jz .whole
    jmp .unpack
.whole:
    mov si, [ar_iset]               ; the header (ARCADE-SPEC 1.4) and the
    mov es, [ar_iclaim]             ;   whole image's CRC-32
    mov word [es:0], 'AR'
    mov word [es:2], 'CR'
    mov word [es:4], 'OM'
    mov word [es:6], 1
    mov di, 8                       ; the set's name: the file's, to its dot
    mov cx, 8
    push si
.nm:
    lodsb
    cmp al, '.'
    jne .nc
    xor al, al
    dec si
.nc:
    stosb
    loop .nm
    pop si
    mov ax, [si+AS_LEN]
    mov [es:16], ax
    mov ax, [si+AS_LEN+2]
    mov [es:18], ax
    mov cx, [si+AS_LEN]             ; (64KB: CX = 0, which ar_crc32 takes)
    push ds
    mov ax, es
    add ax, 2
    mov ds, ax
    push si
    xor si, si
    call ar_crc32
    pop si
    pop ds
    mov [es:20], ax
    mov [es:22], dx
    mov cx, [si+AS_LEN]             ; the file: here, beside the zip
    mov dx, [si+AS_LEN+2]
    add cx, 32
    adc dx, 0
    xor bx, bx
    call OSAPI_FILE_WRITE
    jc .wfail
    inc byte [ar_nimp]
.wfail:
    jmp short .free
.endbad:
    pop dx
    pop ax
.fail:
    pop cx
    inc byte [ar_nwrong]
.free:
    xor dx, dx
    xchg dx, [ar_iclaim]
    call OSAPI_MEM_FREE
    ret

; ar_remember - the folder the instance stands in, kept in ARCADE.DAT if it
; is not there yet (the newest first; the oldest of AR_NPLACE goes)
ar_remember:
    call OSAPI_FILE_HERE
    mov si, ar_places + 1
    mov cl, [ar_places]
    xor ch, ch
.k:
    jcxz .new
    cmp [si], bl
    jne .n
    cmp [si+1], dx
    je .r                           ; known already
.n:
    add si, 3
    loop .k
.new:
    push ds
    pop es
    mov di, ar_places + 1 + AR_NPLACE * 3 - 1   ; the list one along
    mov si, di
    sub si, 3
    mov cx, (AR_NPLACE - 1) * 3
    std
    rep movsb
    cld
    mov [ar_places + 1], bl
    mov [ar_places + 2], dx
    cmp byte [ar_places], AR_NPLACE
    jae .full
    inc byte [ar_places]
.full:
    mov si, ar_s_dat
    mov bx, ar_places
    mov cx, 1 + AR_NPLACE * 3
    mov di, ar_datst
    call od_write
.r:
    ret

; ar_placesload - ARCADE.DAT read at the start: the places a pick looks in
ar_placesload:
    mov byte [ar_places], 0
    push ds
    pop es
    mov si, ar_s_dat
    mov bx, ar_places
    mov cx, 1 + AR_NPLACE * 3
    xor dx, dx
    call od_read
    jc .none
    cmp byte [ar_places], AR_NPLACE
    jbe .r
.none:
    mov byte [ar_places], 0
.r:
    ret

ar_s_dat:    db 'ARCADE.DAT', 0
ar_s_added:  db 'Arcade: '
ar_s_addn:   db '0 set(s) added', 0
ar_s_noset:  db 'Arcade: no set in it', 0
ar_s_wrongz: db 'Arcade: not a set I know', 0
ar_s_badzip: db 'Arcade: cannot read it', 0
ar_s_bigzip: db 'Arcade: zip too big', 0
ar_s_notzip: db 'Arcade: pick a .ZIP', 0
ar_s_nodlg:  db 'Arcade: no chooser', 0
