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
AR_NSET     equ 8                   ; sets ar_sets may list
ZC_CLAIM    equ 6                   ; a zip's record (ar_zsize..ar_cdn): its claim

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
; = its size. A .ZIP is read whole; any other file means LOOSE CHIPS, the
; folder it is in holding each chip as a file of MAME's name. The board
; picked is let go for the memory, and picked again after
ar_impdone:
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
    mov byte [ar_iloose], 1
    mov si, ar_impname              ; a .ZIP?
.dot:
    lodsb
    or al, al
    jz .kind
    cmp al, '.'
    jne .dot
    lodsw
    or ax, 0x2020
    cmp ax, 'zi'
    jne .kind
    lodsw
    or al, 0x20
    cmp ax, 'p'
    jne .kind
    mov byte [ar_iloose], 0
.kind:
    mov bx, [ar_bd]                 ; room: the board picked, let go
    mov [ar_impbd], bx
    call ar_unselect
    mov byte [ar_nimp], 0
    mov byte [ar_nwrong], 0
    mov byte [ar_zon], 0
    mov word [ar_z2+ZC_CLAIM], 0
    cmp byte [ar_iloose], 0
    je .zip
    mov si, ar_sets                 ; LOOSE: each set tried, chip by chip
    mov byte [ar_iidx], 0
.lset:
    lodsw
    or ax, ax
    jz .made
    push si
    mov si, ax
    call ar_impmake
    pop si
    inc byte [ar_iidx]
    jmp .lset
.zip:
    mov si, ar_impname
    call ar_zread
    jnc .read
    mov si, ar_s_badzip
    cmp al, 1
    jb .say
    mov si, ar_s_bigzip
    je .say
    mov si, ar_s_nomem
    jmp .say
.read:
    mov si, ar_sets                 ; which sets are whole, in the zip or
    mov di, ar_iok                  ;   with their parent's
.find:
    lodsw
    or ax, ax
    jz .found
    push si
    push di
    mov si, ax
    call ar_impfind
    pop di
    pop si
    mov [di], al
    inc di
    jmp .find
.found:
    xor cx, cx                      ; what making them moves: each image
    xor dx, dx                      ;   made (counted as it comes out) and
    mov si, ar_sets                 ;   written
    mov di, ar_iok
.sum:
    lodsw
    or ax, ax
    jz .summed
    cmp byte [di], 0
    je .snext
    mov bx, ax
    mov ax, [bx+AS_LEN]
    mov bx, [bx+AS_LEN+2]
    add ax, 32
    adc bx, 0
    shl ax, 1
    rcl bx, 1
    add cx, ax
    adc dx, bx
.snext:
    inc di
    jmp .sum
.summed:
    call ar_opbeg
    mov si, ar_sets
    mov byte [ar_iidx], 0
.make:
    lodsw
    or ax, ax
    jz .madez
    mov bl, [ar_iidx]
    xor bh, bh
    cmp byte [ar_iok+bx], 0
    je .mnext
    push si
    mov si, ax
    call ar_impmake
    pop si
.mnext:
    inc byte [ar_iidx]
    jmp .make
.madez:
    call ar_opend
    call ar_zfree
.made:
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
.say:
    call ar_toast
    call ar_gohome                  ; (the chooser left us in the zip's folder)
    call af_look                    ; the games, looked for again (11.1)
    mov si, [ar_win]
    jmp ar_repaint

; ar_opbeg - DX:CX = bytes: the menu bar's file progress (SPEC.md 12.8.10)
; armed for them, if it can be; ar_opend ends only what this began
ar_opbeg:
    mov byte [ar_iop], 0
    call OSAPI_FS_OP
    jc .r
    mov byte [ar_iop], 1
.r:
    ret
ar_opend:
    pushf                           ; (flags kept: a read's answer rides)
    cmp byte [ar_iop], 0
    je .r
    mov byte [ar_iop], 0
    push cx
    push dx
    xor cx, cx
    xor dx, dx
    call OSAPI_FS_OP
    pop dx
    pop cx
.r:
    popf
    ret

; ar_zread - SI = a zip's name, here: read whole into a claim of its own and
; its directory found (ar_zsize..ar_cdn). CF=1 AL = 0 unreadable or not a
; zip, 1 too big, 2 no memory - with nothing claimed
ar_zread:
    push si
    push ds
    pop es
    xor bx, bx
    xor cx, cx
    xor dx, dx
    call OSAPI_FILE_READ            ; the size probe
    pop si
    jnc .bad                        ; (empty)
    cmp ax, FERR_BIG
    jne .bad
    mov ax, dx                      ; the KB it needs, and two over
    cmp ax, AR_ZMAXKB - 2           ;   (64MB and more answers 0xFFFF)
    ja .big
    add ax, 2
    mov [ar_zkb], ax
    push si
    call OSAPI_MEM_CLAIM
    pop si
    jc .nomem
    mov [ar_zclaim], dx
    mov es, dx
    mov ax, [ar_zkb]
    mov dx, ax
    mov cl, 6
    shr dx, cl
    mov cl, 10
    shl ax, cl
    mov cx, ax
    call ar_opbeg                   ; what the read moves, on the bar
    xor bx, bx
    call OSAPI_FILE_READ
    call ar_opend
    jc .unread
    mov [ar_zsize], ax              ; what was read
    mov [ar_zsize+2], dx
    call ar_zeocd
    jc .unread
    clc
    ret
.unread:
    xor dx, dx
    xchg dx, [ar_zclaim]
    call OSAPI_MEM_FREE
.bad:
    mov al, 0
    stc
    ret
.big:
    mov al, 1
    stc
    ret
.nomem:
    mov al, 2
    stc
    ret

; ar_zswap - the zip in ar_zsize..ar_cdn and the one in ar_z2 change places
ar_zswap:
    push ax
    push cx
    push si
    mov si, ar_zsize
    mov cx, 8
.w:
    mov ax, [si]
    xchg ax, [si + ar_z2 - ar_zsize]
    mov [si], ax
    inc si
    inc si
    loop .w
    xor byte [ar_zon], 1
    pop si
    pop cx
    pop ax
    ret

; ar_gohome - back to the folder ARCADE started in: the chooser moves the
; instance to the file it opens, and ARCADE.O88's parts are read by name in
; the folder it stands in (MEASURED: "Cannot read my parts" at the pick after
; an import from a folder of its own)
ar_gohome:
    mov dx, [ar_homecl]
    mov bl, [ar_homedrv]
    call OSAPI_FILE_GOTO_QM
    ret

; ar_zfree - the zips' claims given back, the parent's too
ar_zfree:
    cmp byte [ar_zon], 0
    je .a
    call ar_zswap
.a:
    xor dx, dx
    xchg dx, [ar_zclaim]
    or dx, dx
    jz .b
    call OSAPI_MEM_FREE
.b:
    xor dx, dx
    xchg dx, [ar_z2+ZC_CLAIM]
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

; ar_zfind - DS:SI = a chip (arcsets.inc): ES:BX = the central directory
; entry holding it, BY ITS CRC-32 AND SIZE, in any folder - a merged set
; keeps a clone's chips in a folder of the clone's name, and a chip two sets
; share is the same bytes under either name. The zip, and then the parent's
; if one is read; the one it was found in is left in ar_zsize..ar_cdn. CF=1
; none. Kept: SI
ar_zfind:
    cmp byte [ar_zon], 0
    je .a
    call ar_zswap
.a:
    call ar_zscan
    jnc .r
    cmp word [ar_z2+ZC_CLAIM], 0
    je .no
    call ar_zswap
    call ar_zscan
    jnc .r
    call ar_zswap
.no:
    stc
.r:
    ret

; ar_zscan - ar_zfind in the zip in ar_zsize..ar_cdn alone
ar_zscan:
    push cx
    push dx
    mov es, [ar_cdseg]
    mov bx, [ar_cdoff]
    mov cx, [ar_cdn]
.e:
    jcxz .none
    mov ax, bx                      ; the entry, wholly inside the directory
    add ax, 46
    jc .none
    cmp ax, [ar_cdend]
    ja .none
    cmp word [es:bx], 'PK'
    jne .none
    cmp word [es:bx+2], 0x0201
    jne .none
    mov ax, [es:bx+16]              ; its CRC-32 and size, stored or deflated
    cmp ax, [si+AC_CRC]
    jne .no
    mov ax, [es:bx+18]
    cmp ax, [si+AC_CRC+2]
    jne .no
    mov ax, [es:bx+24]
    cmp ax, [si+AC_LEN]
    jne .no
    cmp word [es:bx+26], 0
    jne .no
    cmp word [es:bx+22], 0          ; (packed under 64KB, as ar_impmake wants)
    jne .no
    mov ax, [es:bx+10]
    cmp ax, 8
    je .yes
    or ax, ax
    jz .yes
.no:
    mov ax, [es:bx+28]              ; the next: 46 + name + extra + comment
    add ax, [es:bx+30]
    jc .none
    add ax, [es:bx+32]
    jc .none
    add ax, 46
    jc .none
    add bx, ax
    jc .none
    dec cx
    jmp .e
.yes:
    clc
    jmp short .out
.none:
    stc
.out:
    pop dx
    pop cx
    ret

; ar_impset's table (arcsets.inc, tools/getarcrom.py --asm-sets)
AS_NAME     equ 0                   ; a set: its .ROM's name (13)
AS_LEN      equ 13                  ;   the image's length (dword)
AS_PAR      equ 17                  ;   MAME's parent zip, for a clone (13)
AS_N        equ 30                  ;   its chips
AS_CHIPS    equ 31                  ;   ...each:
AC_NAME     equ 0                   ;   its name (13)
AC_AT       equ 13                  ;   its place and size in the image
AC_LEN      equ 15
AC_CRC      equ 17                  ;   its CRC-32
AC_SIZE     equ 21

; ar_impfind - SI -> a set: AL = 1 if every chip is in the zip. A set some
; of whose chips are there and some not is a clone of a SPLIT set, whose
; other chips are its parent's: the parent's zip is looked for beside this
; one, read, and the set looked for again. Still short, it counts in
; [ar_nwrong]. Kept: nothing but DS
ar_impfind:
    call .count
    cmp al, [si+AS_N]
    je .whole
    or al, al
    jz .not                         ; none of it: not this set
    cmp byte [si+AS_PAR], 0         ; a clone, and no parent read yet
    je .short
    cmp word [ar_z2+ZC_CLAIM], 0
    jne .short
    push si
    lea di, [si+AS_PAR]             ; (not the zip picked, again)
    mov bx, ar_impname
.same:
    mov al, [di]
    mov ah, [bx]
    or ax, 0x2020
    cmp al, ah
    jne .other
    inc di
    inc bx
    cmp byte [di-1], 0
    jne .same
    pop si
    jmp short .short
.other:
    pop si
    push si
    call ar_zswap                   ; the parent's, into ar_zsize..ar_cdn
    lea si, [si+AS_PAR]
    call ar_zread
    jnc .two
    mov word [ar_zclaim], 0         ; (nothing claimed: ar_z2 stays empty)
.two:
    call ar_zswap
    pop si
    call .count
    cmp al, [si+AS_N]
    je .whole
.short:
    inc byte [ar_nwrong]
.not:
    mov al, 0
    ret
.whole:
    mov al, 1
    ret
; .count - AL = the chips of set SI found
.count:
    push si
    mov cl, [si+AS_N]
    xor ch, ch
    lea si, [si+AS_CHIPS]
    xor dx, dx
.c:
    push cx
    push dx
    call ar_zfind
    pop dx
    pop cx
    jc .cn
    inc dx
.cn:
    add si, AC_SIZE
    loop .c
    mov al, dl
    pop si
    ret

; ar_impmake - SI -> a set whose chips are all to hand: each unpacked from
; the zip(s) - or, LOOSE ([ar_iloose]), read from its own file here - into
; the image, checked against its CRC-32 as it lands, and the image written
; as <SET>.ROM here with its header (ARCADE-SPEC 1.4). [ar_nimp] counts it,
; [ar_nwrong] a chip that comes out wrong. A loose set whose FIRST chip has
; no file is just not here, and says nothing
ar_impmake:
    mov [ar_iset], si
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
    jnc .claimed
    ret
.claimed:
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
    mov byte [ar_ifound], 0
.unpack:
    push cx
    cmp byte [ar_iloose], 0
    je .fromzip
    mov ax, [si+AC_AT]              ; LOOSE: the chip's file, read straight
    mov cl, 4                       ;   into its place (every place is a
    shr ax, cl                      ;   multiple of 16, as the read wants)
    add ax, [ar_iclaim]
    add ax, 2
    mov es, ax
    xor bx, bx
    mov cx, [si+AC_LEN]
    xor dx, dx
    push si
    lea si, [si+AC_NAME]
    call OSAPI_FILE_READ
    pop si
    jc .lmiss
    or dx, dx
    jnz .fail
    cmp ax, [si+AC_LEN]
    jne .fail
    jmp .check
.lmiss:
    cmp byte [ar_ifound], 0         ; the first chip not here: not this set
    jne .fail
    pop cx
    jmp .free
.fromzip:
    call ar_zfind                   ; its local header, and its data
    jc .fail
    mov ax, [es:bx+42]
    mov dx, [es:bx+44]
    mov cx, [es:bx+10]
    mov [ar_imeth], cx
    mov cx, [es:bx+20]              ; the packed size, for the bounds
    mov [ar_ipack], cx
    call ar_zat
    jc .fail
    cmp word [es:bx], 'PK'
    jne .fail
    cmp word [es:bx+2], 0x0403
    jne .fail
    mov cx, [es:bx+26]
    add cx, [es:bx+28]
    jc .fail
    add cx, 30
    jc .fail
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
.check:
    inc byte [ar_ifound]
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
    mov ax, [si+AC_LEN]             ; made: on the bar
    call OSAPI_FS_PROG
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
    jc .free
    inc byte [ar_nimp]
    call OSAPI_FILE_HERE            ; the set's drive, for "disk in B:"
    mov al, [ar_iidx]
    cmp al, AR_NSET
    jae .free
    cbw
    xchg ax, bx
    mov [ar_sdrv+bx], al
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
    je .full                        ; known already (the drives still written)
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
    jmp ar_datwrite

; ar_placesload - ARCADE.DAT read at the start: the places a pick looks in,
; and the drive each set was last imported on
ar_placesload:
    push ds
    pop es
    mov si, ar_s_dat
    mov bx, ar_places
    mov cx, AR_DATSZ
    xor dx, dx
    call od_read
    jc .none
    cmp byte [ar_places], AR_NPLACE
    jbe .r
.none:
    mov byte [ar_places], 0
    mov di, ar_sdrv                 ; no drives, no switches set, no bests
    mov cx, AR_NSET * 2
    mov al, 0xFF
    cld
    rep stosb
    mov cx, AR_NSET * AR_HSB
    xor al, al
    rep stosb
.r:
    ret

; ar_setdrv - SI = a .ROM's name: AL = the drive its set was last imported
; on (0 = A:), CF=1 none known. Kept: all else
ar_setdrv:
    push bx
    push cx
    push di
    push si
    mov di, ar_sets
    xor cx, cx
.s:
    mov bx, [di]
    or bx, bx
    jz .no
    pop si
    push si
.c:
    lodsb
    cmp al, [bx]
    jne .n
    inc bx
    or al, al
    jnz .c
    mov bx, cx                      ; this one
    cmp bx, AR_NSET
    jae .no
    mov al, [ar_sdrv+bx]
    cmp al, 26
    jae .no
    clc
    jmp short .r
.n:
    inc di
    inc di
    inc cx
    jmp .s
.no:
    stc
.r:
    pop si
    pop di
    pop cx
    pop bx
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
