; =============================================================================
; os8088 - CDROM.DRV / cdiso.inc - the volume (SPEC.md 119.3)
;
; ISO 9660 answered as SPEC.md 62.9's FSV_* verbs: a directory walk over the
; tagged sector buffer, 8.3 names made the same way every time (119.3.2),
; handles that name a directory record by WHERE IT IS (119.3.3), and audio
; tracks listed as TRACKnn.CDA files (119.3.5).
;
; THE KERNEL STILL SORTS (19.4) AND STILL SYNTHESIZES `..` (19.5): the walk
; hands records over in disc order and never lists `.` or `..`.
; =============================================================================

CD_HC_N      equ 4              ; handles cd_resolve remembers
CD_HC_SZ     equ 12             ; a row: handle, extent, length, flags, track
CD_SPAN      equ 1022           ; directory sectors above cd_dbase that encode
CD_HCDA      equ 0xFF80         ; + n = audio track n's TRACKnn.CDA
CD_RUN       equ 16             ; sectors a READ (10) moves straight into the
                                ; caller's buffer: 32 KB, inside one normalised
                                ; segment
CDR_LEN      equ 0              ; a directory record (ISO 9660 9.1)
CDR_EXT      equ 2              ; dd: the extent, little-endian half
CDR_SIZE     equ 10             ; dd: the data length, little-endian half
CDR_FLAGS    equ 25             ; bit 0 hidden, 1 directory, 2 associated
CDR_NLEN     equ 32
CDR_NAME     equ 33

; =============================================================================
; THE WALK
; =============================================================================

; -----------------------------------------------------------------------------
; cd_dopen - DX:AX = an extent, CX:BX = its length in bytes: the walk starts
;            at its first record. clobbers: nothing
; -----------------------------------------------------------------------------
cd_dopen:
    push ax
    push bx
    push cx
    push dx
    mov [cd_it_lba], ax
    mov [cd_it_lba+2], dx
    mov ax, bx
    mov dx, cx
    add ax, 2047
    adc dx, 0
    mov cl, 11
    shr ax, cl
    mov cl, 5
    shl dx, cl
    or ax, dx                   ; sectors: a directory is far under 64K of them
    mov [cd_it_n], ax
    mov word [cd_it_off], 0
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cd_dnext - the walk's next record
; out: CF = 0 and SI = the record, in cd_sbuf, its sector in [cd_it_lba];
;      CF = 1 and AX = 0 at the end, or AX = a CDE_*
; clobbers: AX
;
; A zero length is the sector's padding - records never cross a sector - and
; so is anything shorter than the 34 bytes the smallest record is.
; -----------------------------------------------------------------------------
cd_dnext:
    push dx
.sec:
    cmp word [cd_it_n], 0
    je .end
    mov ax, [cd_it_lba]
    mov dx, [cd_it_lba+2]
    call cd_rdsec
    jc .out
    mov si, [cd_it_off]
    cmp si, 2048 - 34
    ja .nextsec
    mov al, [cd_sbuf+si]
    xor ah, ah
    cmp al, 34
    jb .nextsec
    add ax, si
    cmp ax, 2048
    ja .nextsec                 ; a record across the sector: not a disc
    mov [cd_it_off], ax
    add si, cd_sbuf
    clc
    jmp short .out
.nextsec:
    dec word [cd_it_n]
    add word [cd_it_lba], 1
    adc word [cd_it_lba+2], 0
    mov word [cd_it_off], 0
    jmp short .sec
.end:
    xor ax, ax
    stc
.out:
    pop dx
    ret

; -----------------------------------------------------------------------------
; cd_vnext - the walk's next LISTED record (119.3.2)
; out: CF = 0, SI = the record and AX = its handle; CF = 1 as cd_dnext
; clobbers: AX
;
; Not `.` or `..` (a one-byte name, 00h or 01h), not hidden (flag bit 0), not
; associated (bit 2), and not a record whose place does not encode (119.3.3).
; -----------------------------------------------------------------------------
cd_vnext:
    call cd_dnext
    jc .out
    test byte [si+CDR_FLAGS], 1 | 4
    jnz cd_vnext
    cmp byte [si+CDR_NLEN], 1
    jne .named
    cmp byte [si+CDR_NAME], 1
    jbe cd_vnext
.named:
    push ax                     ; a name that runs past its record is no
    push dx                     ; record: listed it would be read from the
    mov al, [si+CDR_NLEN]       ; bytes after it (the next record, or past
    xor ah, ah                  ; cd_sbuf's end)
    add ax, CDR_NAME
    mov dl, [si+CDR_LEN]
    xor dh, dh
    cmp ax, dx
    pop dx
    pop ax
    ja cd_vnext
    call cd_hof
    jc cd_vnext
.out:
    ret

; -----------------------------------------------------------------------------
; cd_hof - SI = a record the walk just gave -> AX = its handle (119.3.3)
; out: CF = 1 = its place does not encode. clobbers: AX
; -----------------------------------------------------------------------------
cd_hof:
    push cx
    push dx
    mov ax, [cd_it_lba]
    mov dx, [cd_it_lba+2]
    sub ax, [cd_dbase]
    sbb dx, [cd_dbase+2]
    jc .out                     ; below the base
    or dx, dx
    jnz .no
    cmp ax, CD_SPAN
    jae .no
    mov cl, 6
    shl ax, cl                  ; (LBA - base) x 64...
    mov dx, si
    sub dx, cd_sbuf
    mov cl, 5
    shr dx, cl                  ; ...+ the offset / 32
    or ax, dx                   ; CF = 0 off the `or`
    jmp short .out
.no:
    stc
.out:
    pop dx
    pop cx
    ret

; =============================================================================
; NAMES (SPEC.md 119.3.2) - tools/os88cdiso.py's mangle() is the reference
; =============================================================================

; CD_NCH - AL = the name's DIth byte (SI = the name). [si+di] is not an 8086
; address, so BX carries the index for the one load
%macro CD_NCH 0
    push bx
    mov bx, di
    mov al, [bx+si]
    pop bx
%endmacro

; cd_upper - AL -> upper case. clobbers: nothing
cd_upper:
    cmp al, 'a'
    jb .out
    cmp al, 'z'
    ja .out
    sub al, 'a' - 'A'
.out:
    ret

; cd_legal - AL (upper case) -> CF = 0 a FAT short-name character: A-Z, 0-9
;            and the kernel's own dskw_syms. clobbers: nothing
cd_legal:
    cmp al, 'A'
    jb .notalpha
    cmp al, 'Z'
    jbe .yes
.notalpha:
    cmp al, '0'
    jb .sym
    cmp al, '9'
    jbe .yes
.sym:
    push si
    mov si, cd_syms
.s:
    cmp byte [si], 0
    je .no
    cmp al, [si]
    je .yes1
    inc si
    jmp short .s
.yes1:
    pop si
.yes:
    clc
    ret
.no:
    pop si
    stc
    ret
cd_syms:    db '$', '%', 0x27, '-', '_', '@', '~', 0x60
            db '!', '(', ')', '{', '}', '^', '#', '&', 0

; cd_put - AL -> the next byte of cd_nm (internal). clobbers: nothing
cd_put:
    push di
    mov di, [cd_opos]
    mov [di], al
    inc di
    mov [cd_opos], di
    pop di
    ret

; -----------------------------------------------------------------------------
; cd_mangle - SI = a record -> cd_nm, its 8.3 name, NUL-ended
; clobbers: nothing
;
; The ;version and trailing dots go; letters are upper-cased; a name that is
; then legal 8.3 is used as it is. Otherwise: six legal characters of the
; base, `~`, one character of the stripped name's byte sum mod 36, and three
; legal characters of the last extension behind a dot.
; -----------------------------------------------------------------------------
cd_mangle:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov word [cd_opos], cd_nm
    mov cl, [si+CDR_NLEN]
    xor ch, ch
    add si, CDR_NAME            ; SI = the name, CX = its length
    xor bx, bx                  ; BX = L: up to the ;version
.scan:
    cmp bx, cx
    jae .trim
    cmp byte [si+bx], ';'
    je .trim
    inc bx
    jmp short .scan
.trim:
    or bx, bx
    jz .trimmed
    cmp byte [si+bx-1], '.'
    jne .trimmed
    dec bx
    jmp short .trim
.trimmed:
    xor dx, dx                  ; DX = the byte sum, upper-cased
    mov word [cd_dot], 0xFFFF   ; ...and where the last dot is
    xor di, di
.h:
    cmp di, bx
    jae .hd
    CD_NCH
    call cd_upper
    xor ah, ah
    add dx, ax
    cmp al, '.'
    jne .hn
    mov [cd_dot], di
.hn:
    inc di
    jmp short .h
.hd:
    mov ax, dx
    xor dx, dx
    mov cx, 36
    div cx                      ; DX = the sum mod 36
    mov al, dl
    add al, '0'
    cmp al, '9'
    jbe .hc
    add al, 'A' - '9' - 1
.hc:
    mov [cd_hash], al
    mov dx, bx                  ; DX = where the base ends...
    mov cx, bx                  ; ...CX = where the extension starts
    mov ax, [cd_dot]
    cmp ax, 0xFFFF
    je .split
    mov dx, ax
    mov cx, ax
    inc cx
.split:
    or dx, dx                   ; a legal 8.3 name as it stands?
    jz .mangle
    cmp dx, 8
    ja .mangle
    mov ax, bx
    sub ax, cx
    cmp ax, 3
    ja .mangle
    xor di, di
.lb:
    cmp di, dx
    jae .le0
    CD_NCH
    call cd_upper
    call cd_legal
    jc .mangle
    inc di
    jmp short .lb
.le0:
    mov di, cx
.le:
    cmp di, bx
    jae .plain
    CD_NCH
    call cd_upper
    call cd_legal
    jc .mangle
    inc di
    jmp short .le
.plain:                         ; the base, and the extension behind a dot
    xor di, di
.pb:
    cmp di, dx
    jae .pe0
    CD_NCH
    call cd_upper
    call cd_put
    inc di
    jmp short .pb
.pe0:
    cmp cx, bx
    jae .done
    mov al, '.'
    call cd_put
    mov di, cx
.pe:
    cmp di, bx
    jae .done
    CD_NCH
    call cd_upper
    call cd_put
    inc di
    jmp short .pe
.mangle:                        ; six legal characters of the base...
    xor di, di
    xor ah, ah                  ; AH = how many went in
.mb:
    cmp di, dx
    jae .mbd
    cmp ah, 6
    jae .mbd
    CD_NCH
    call cd_upper
    call cd_legal
    jc .mbn
    call cd_put
    inc ah
.mbn:
    inc di
    jmp short .mb
.mbd:
    or ah, ah
    jnz .tilde
    mov al, '_'                 ; ...or one that stands for none
    call cd_put
.tilde:
    mov al, '~'
    call cd_put
    mov al, [cd_hash]
    call cd_put
    mov di, cx                  ; ...and three of the extension
    xor ah, ah
.me:
    cmp di, bx
    jae .done
    cmp ah, 3
    jae .done
    CD_NCH
    call cd_upper
    call cd_legal
    jc .men
    or ah, ah
    jnz .mep
    push ax
    mov al, '.'
    call cd_put
    pop ax
.mep:
    call cd_put
    inc ah
.men:
    inc di
    jmp short .me
.done:
    xor al, al
    call cd_put
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; STAGING - SPEC.md 19.1's 32-byte entry, which FSV_LIST appends and FSV_ENUM
; hands over
; =============================================================================

; cd_ezero - cd_ent all zero, ES left alone. clobbers: nothing
cd_ezero:
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    mov di, cd_ent
    xor ax, ax
    mov cx, 16
    cld
    rep stosw
    pop es
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cd_stage - SI = a record, AX = its handle -> cd_ent. clobbers: nothing
; -----------------------------------------------------------------------------
cd_stage:
    push ax
    push bx
    push si
    push di
    call cd_ezero
    mov [cd_ent+18], ax         ; @18: the handle
    mov bl, [si+CDR_FLAGS]
    test bl, 2
    jnz .nosize                 ; @20: the size - and a folder's is 0, as a
    mov ax, [si+CDR_SIZE]       ; FAT folder's is (SPEC.md 19.1), whatever
    mov [cd_ent+20], ax         ; its extent's length
    mov ax, [si+CDR_SIZE+2]
    mov [cd_ent+22], ax
.nosize:
    call cd_mangle
    mov si, cd_nm               ; @0: the name
    mov di, cd_ent
.cp:
    lodsb
    mov [di], al
    inc di
    or al, al
    jnz .cp
    mov ax, 2                   ; @16: 2 a folder...
    test bl, 2
    jnz .type
    xor ax, ax                  ; ...1 a package, 0 anything else
    cmp di, cd_ent + 5
    jb .type
    cmp word [di-5], '.O'
    jne .type
    cmp word [di-3], '88'
    jne .type
    inc ax
.type:
    mov [cd_ent+16], ax
    pop di
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cd_isaudio - AL = a track -> CF = 0 an audio track on this disc
; clobbers: nothing
; -----------------------------------------------------------------------------
cd_isaudio:
    cmp al, [cd_first]
    jb .no
    cmp al, [cd_last]
    ja .no
    push si
    call cd_tdesc
    test byte [si+1], CDT_DATA
    pop si
    jnz .no
    clc
    ret
.no:
    stc
    ret

; cd_cdaname - AL = a track -> cd_nm = 'TRACKnn.CDA'. clobbers: nothing
cd_cdaname:
    push ax
    push di
    mov di, cd_nm
    mov word [di], 'TR'
    mov word [di+2], 'AC'
    mov byte [di+4], 'K'
    xor ah, ah
    push cx
    mov cl, 10
    div cl                      ; AL = tens, AH = units
    pop cx
    add ax, '00'
    mov [di+5], ax
    mov word [di+7], '.C'
    mov word [di+9], 'DA'
    mov byte [di+11], 0
    pop di
    pop ax
    ret

; -----------------------------------------------------------------------------
; cd_stagecda - AL = an audio track -> cd_ent: TRACKnn.CDA, 44 bytes, type 0
; clobbers: nothing
; -----------------------------------------------------------------------------
cd_stagecda:
    push ax
    push cx
    push si
    push di
    call cd_ezero
    call cd_cdaname
    xor ah, ah
    add ax, CD_HCDA
    mov [cd_ent+18], ax
    mov word [cd_ent+20], 44
    push es
    push ds
    pop es
    mov si, cd_nm
    mov di, cd_ent
    mov cx, 12
    cld
    rep movsb
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cd_mkcda - [cd_r_cda]'s 44 bytes into cd_cda (119.3.5). clobbers: nothing
; -----------------------------------------------------------------------------
cd_mkcda:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov word [cd_cda], 'RI'     ; every one of the 44 bytes is written below
    mov word [cd_cda+2], 'FF'
    mov word [cd_cda+4], 36
    mov word [cd_cda+6], 0
    mov word [cd_cda+8], 'CD'
    mov word [cd_cda+10], 'DA'
    mov word [cd_cda+12], 'fm'
    mov word [cd_cda+14], 't '
    mov word [cd_cda+16], 24
    mov word [cd_cda+18], 0
    mov word [cd_cda+20], 1
    mov al, [cd_r_cda]
    xor ah, ah
    mov [cd_cda+22], ax         ; the track
    mov al, [cd_last]           ; the disc id: the lead-out, xor the track
    inc ax                      ; count in its top byte
    call cd_tstart
    mov cl, [cd_last]
    sub cl, [cd_first]
    inc cl
    xor dh, cl
    mov [cd_cda+24], ax
    mov [cd_cda+26], dx
    mov al, [cd_r_cda]
    call cd_tstart              ; the start...
    mov [cd_cda+28], ax
    mov [cd_cda+30], dx
    push ax
    push dx
    mov al, [cd_r_cda]
    call cd_tend                ; ...and the length, the track's end less its
    pop cx                      ; start (cd_tend: CD-Extra's gap is not ours)
    pop bx
    sub ax, bx
    sbb dx, cx
    mov [cd_cda+32], ax
    mov [cd_cda+34], dx
    push ax
    push dx
    mov ax, bx
    mov dx, cx
    add ax, 150                 ; MSF counts the two-second lead-in
    adc dx, 0
    mov di, cd_cda + 36
    call cd_msfle
    pop dx
    pop ax
    mov di, cd_cda + 40
    call cd_msfle
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; cd_msfle - DX:AX frames -> [DI] = F, S, M, 0. clobbers: AX, DX
cd_msfle:
    push cx
    or dx, dx                   ; cd_msf's two clamps, for the divide's sake
    jns .pos
    xor ax, ax
    xor dx, dx
.pos:
    cmp dx, 4500
    jb .ok
    mov dx, 4499
.ok:
    mov cx, 4500                ; frames a minute
    div cx                      ; AX = minutes, DX = the rest
    mov [di+2], al
    mov byte [di+3], 0
    mov ax, dx
    mov cl, 75
    div cl                      ; AL = seconds, AH = frames
    mov [di+1], al
    mov [di], ah
    pop cx
    ret

; =============================================================================
; RESOLVING A HANDLE (SPEC.md 119.3.3)
; =============================================================================

; -----------------------------------------------------------------------------
; cd_resolve - AX = a handle -> what it names
; out: CF = 0 and [cd_r_lba], [cd_r_len], [cd_r_fl], [cd_r_cda]; CF = 1 and
;      AX = a FERR_*
; clobbers: AX (on a refusal)
; -----------------------------------------------------------------------------
cd_resolve:
    push bx
    push cx
    push dx
    push si
    push di
    mov byte [cd_r_cda], 0
    or ax, ax
    jnz .notroot
    mov si, cd_rlba             ; 0: the root
    mov dl, 2
    jmp .take
.notroot:
    cmp ax, CD_HCDA
    jb .rec
    sub ax, CD_HCDA             ; an audio track's file
    call cd_isaudio
    jc .noent
    mov [cd_r_cda], al
    mov word [cd_r_len], 44
    mov word [cd_r_len+2], 0
    mov byte [cd_r_fl], 0
    jmp .ok
.rec:
    cmp byte [cd_iso], 0
    je .noent
    mov si, cd_hcache           ; remembered?
    mov cx, CD_HC_N
.c:
    cmp [si], ax
    je .hit
    add si, CD_HC_SZ
    loop .c
    mov bx, ax                  ; no: read its sector and walk to it
    and bx, 63                  ; BX = the slot
    push ax
    mov cl, 6
    shr ax, cl
    xor dx, dx
    add ax, [cd_dbase]
    adc dx, [cd_dbase+2]
    call cd_rdsec
    pop ax
    jc .io
    xor si, si
.w:
    cmp si, 2048 - 34
    ja .noent
    mov cl, [cd_sbuf+si]
    xor ch, ch
    cmp cl, 34
    jb .noent
    mov di, si
    push cx
    mov cl, 5
    shr di, cl
    pop cx
    cmp di, bx
    je .found
    ja .noent
    add si, cx
    jmp short .w
.found:
    add si, cd_sbuf
    mov dl, [si+CDR_NLEN]       ; a name that runs past the record is no record
    xor dh, dh
    add dx, CDR_NAME
    push ax
    mov al, [si+CDR_LEN]        ; ...compared in 16 bits: a 223-byte name
    xor ah, ah                  ; plus 33 wraps a byte
    cmp dx, ax
    pop ax
    ja .noent
    mov dl, [si+CDR_FLAGS]
    push ax                     ; the handle, into the next cache row
    push si
    mov al, [cd_hcnext]
    mov ah, CD_HC_SZ
    mul ah
    add ax, cd_hcache
    xchg di, ax
    pop si
    pop ax
    mov [di], ax
    mov bx, [si+CDR_EXT]
    mov [di+2], bx
    mov bx, [si+CDR_EXT+2]
    mov [di+4], bx
    mov bx, [si+CDR_SIZE]
    mov [di+6], bx
    mov bx, [si+CDR_SIZE+2]
    mov [di+8], bx
    mov [di+10], dl
    inc byte [cd_hcnext]
    and byte [cd_hcnext], CD_HC_N - 1
    lea si, [di+2]
    jmp short .take
.hit:
    mov dl, [si+10]
    add si, 2
.take:                          ; SI = extent then length, DL = the flags
    mov ax, [si]
    mov [cd_r_lba], ax
    mov ax, [si+2]
    mov [cd_r_lba+2], ax
    mov ax, [si+4]
    mov [cd_r_len], ax
    mov ax, [si+6]
    mov [cd_r_len+2], ax
    mov [cd_r_fl], dl
.ok:
    clc
    jmp short .out
.io:
    mov ax, FERR_IO
    stc
    jmp short .out
.noent:
    mov ax, FERR_NOENT
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret
%if CD_HC_N & (CD_HC_N - 1)
  %error "cdrom: cd_hcnext wraps with an AND, so CD_HC_N is a power of two"
%endif

; =============================================================================
; THE FSV_* VERBS (SPEC.md 62.9.1, 119.3.4)
; =============================================================================

; -----------------------------------------------------------------------------
; cd_ferr - AX = a CDE_* -> the FERR_* a verb answers: an empty drive is
;           FERR_NODISK, the rest FERR_IO. CF = 1, DX = 0 (a READ's count)
; -----------------------------------------------------------------------------
cd_ferr:
    cmp ax, CDE_NODISC
    mov ax, FERR_NODISK
    je .out
    mov ax, FERR_IO
.out:
    xor dx, dx
    stc
    ret

; FSV_WRITE, APPEND, DELETE, RENAME, MKDIR, RMDIR: a CD is read-only
cd_wprot:
    mov ax, FERR_WPROT
    stc
    ret

; FSV_DFREE: nothing free, in 2,048-byte granules
cd_dfree:
    xor ax, ax
    xor dx, dx
    mov bx, 2048
    clc
    ret

; cd_cwdopen - the walk at the current folder's first record. clobbers: nothing
cd_cwdopen:
    push ax
    push bx
    push cx
    push dx
    mov ax, [cd_cwdlba]
    mov dx, [cd_cwdlba+2]
    mov bx, [cd_cwdlen]
    mov cx, [cd_cwdlen+2]
    call cd_dopen
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; FSV_CHDIR - AX = a folder's handle, 0 = the root
; out: CF = 0 and DX = the parent's handle; CF = 1 and AX = a FERR_*
;
; The root clears [cd_stale] (119.3.4): it is the one folder whose handle no
; disc change can falsify, so a mount of it is what makes handles good again.
; -----------------------------------------------------------------------------
cd_chdir:
    push bx
    push cx
    push si
    push di
    push ax
    call cd_media
    jnc .known
    pop cx                      ; the handle, dropped: AX is the CDE_*
    call cd_ferr
    jmp short .out
.known:
    pop ax
    or ax, ax
    jz .root
    cmp byte [cd_stale], 0
    jne .noent
    call cd_resolve
    jc .out
    test byte [cd_r_fl], 2
    jz .noent                   ; nothing chdirs into a file
    mov [cd_cwd], ax
    mov si, cd_r_lba
    call cd_setcwd
    call cd_parent              ; DX = the parent's handle
    clc
    jmp short .out
.root:
    mov byte [cd_stale], 0
    mov [cd_cwd], ax
    mov si, cd_rlba
    call cd_setcwd
    xor dx, dx                  ; the root's parent is never read
    clc
    jmp short .out
.noent:
    mov ax, FERR_NOENT
    stc
.out:
    pop di
    pop si
    pop cx
    pop bx
    ret

; cd_setcwd - SI = an extent then a length (8 bytes) -> the current folder's
cd_setcwd:
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    mov di, cd_cwdlba
    mov cx, 4
    cld
    rep movsw
    pop es
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cd_parent - the current folder's parent's handle, into DX (119.3.3)
; clobbers: nothing else
;
; The folder's own `..` names the parent's extent; the parent's `..` names
; the grandparent; and the parent's handle is the record in the grandparent
; whose extent is the parent's. Anything that does not read answers the root:
; a `..` that goes one level too high is a folder the user can still leave.
; -----------------------------------------------------------------------------
cd_parent:
    push ax
    push bx
    push cx
    push si
    mov ax, [cd_cwdlba]
    mov dx, [cd_cwdlba+2]
    call cd_dotdot              ; DX:AX = the parent's extent
    jc .root
    cmp ax, [cd_rlba]
    jne .notroot
    cmp dx, [cd_rlba+2]
    je .root
.notroot:
    mov [cd_tmp], ax
    mov [cd_tmp+2], dx
    call cd_dotdot              ; the grandparent's extent
    jc .root
    mov [cd_tmp2], ax
    mov [cd_tmp2+2], dx
    call cd_rdsec               ; its `.` record carries its length
    jc .root
    mov bx, [cd_sbuf+CDR_SIZE]
    mov cx, [cd_sbuf+CDR_SIZE+2]
    call cd_dopen
.w:
    call cd_dnext
    jc .root
    test byte [si+CDR_FLAGS], 2
    jz .w
    cmp byte [si+CDR_NLEN], 1
    jne .cmp
    cmp byte [si+CDR_NAME], 1
    jbe .w                      ; its own `.` and `..`
.cmp:
    mov ax, [si+CDR_EXT]
    cmp ax, [cd_tmp]
    jne .w
    mov ax, [si+CDR_EXT+2]
    cmp ax, [cd_tmp+2]
    jne .w
    call cd_hof
    jc .root
    xchg dx, ax
    jmp short .out
.root:
    xor dx, dx
.out:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; cd_dotdot - DX:AX = a directory's extent -> DX:AX = its parent's, out of the
;             second record of its first sector; CF = 1 unreadable
;             clobbers: SI
cd_dotdot:
    call cd_rdsec
    jc .out
    mov si, cd_sbuf
    push ax
    mov al, [si]
    xor ah, ah
    cmp al, 34
    jb .bad
    add si, ax
    pop ax
    mov ax, [si+CDR_EXT]
    mov dx, [si+CDR_EXT+2]
    clc
.out:
    ret
.bad:
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; FSV_LIST - append the current folder's entries, then (the root only) the
;            audio tracks'. out: CF = 0, whatever the drive said: a mount that
;            got this far has already been answered by FSV_CHDIR
; -----------------------------------------------------------------------------
cd_list:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call cd_media
    jc .out
    cmp byte [cd_stale], 0
    jne .out
    cmp byte [cd_iso], 0
    je .tracks
    call cd_cwdopen
.rec:
    call cd_vnext
    jc .tracks
    call cd_stage
    mov si, cd_ent              ; ES:SI - the X stub makes ES our DS
    call OSAPI_FS_ENT
    jc .out                     ; the listing is full: stop cleanly
    jmp short .rec
.tracks:
    cmp word [cd_cwd], 0
    jne .out
    mov al, [cd_first]
.t:
    call cd_isaudio
    jc .tn
    call cd_stagecda
    mov si, cd_ent
    push ax
    call OSAPI_FS_ENT
    pop ax
    jc .out
.tn:
    inc al
    cmp al, [cd_last]
    jbe .t
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; FSV_ENUM - AX = a folder's handle (0 = the root), CX = the ordinal, DX:BX =
;            a 32-byte buffer. out: CF = 0 and the buffer is the entry FSV_LIST
;            would have appended CXth; CF = 1 and AX = 0 past the end, or a
;            FERR_*
; -----------------------------------------------------------------------------
cd_enum:
    push bx
    push cx
    push dx
    push si
    push di
    mov [cd_bseg], dx
    mov [cd_boff], bx
    mov [cd_ord], cx
    push ax
    call cd_media
    jnc .known
    pop cx
    call cd_ferr
    jmp .out
.known:
    pop ax
    mov si, cd_rlba
    or ax, ax
    jz .walk
    cmp byte [cd_stale], 0
    jne .noent
    call cd_resolve
    jc .out
    test byte [cd_r_fl], 2
    jz .noent
    mov si, cd_r_lba
.walk:
    push ax                     ; the folder: 0 is the root, whose tracks
    cmp byte [cd_iso], 0        ; follow its records
    je .tracks
    mov ax, [si]
    mov dx, [si+2]
    mov bx, [si+4]
    mov cx, [si+6]
    call cd_dopen
.rec:
    call cd_vnext
    jnc .counted
    or ax, ax
    jz .tracks                  ; the folder's end: then the root's tracks
    pop cx                      ; a READ that failed: an I/O error, and NOT
    call cd_ferr                ; "past the end" - a copy would stop early and
    jmp short .out              ; call that success (SPEC.md 62.9.7)
.counted:
    cmp word [cd_ord], 0
    je .stage
    dec word [cd_ord]
    jmp short .rec
.stage:
    call cd_stage
    pop ax
    jmp short .give
.tracks:
    pop ax
    or ax, ax
    jnz .end
    mov al, [cd_first]
.t:
    call cd_isaudio
    jc .tn
    cmp word [cd_ord], 0
    je .cda
    dec word [cd_ord]
.tn:
    inc al
    cmp al, [cd_last]
    jbe .t
.end:
    xor ax, ax                  ; past the end: how every walk finishes
    stc
    jmp short .out
.cda:
    call cd_stagecda
.give:
    push es
    mov es, [cd_bseg]
    mov di, [cd_boff]
    mov si, cd_ent
    mov cx, DSK_DE_SIZE / 2
    cld
    rep movsw
    pop es
    clc
    jmp short .out
.noent:
    mov ax, FERR_NOENT
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; FSV_STAT - SI = a NUL 8.3 name in KERNEL_SEG
; out: CF = 0, AX = the handle, DX:CX = the size, BL = 10h a folder / 0;
;      CF = 1 and AX = a FERR_*
; -----------------------------------------------------------------------------
cd_stat:
    push si
    push di
    mov di, cd_want             ; the name, staged and upper-cased
    mov cx, 13
.c:
    mov al, [es:si]
    call cd_upper
    mov [di], al
    or al, al
    jz .staged
    inc si
    inc di
    loop .c
    mov byte [di], 0
.staged:
    call cd_media
    jnc .known
    call cd_ferr
    jmp .out
.known:
    cmp byte [cd_stale], 0
    jne .noent
    cmp byte [cd_iso], 0
    je .cda
    call cd_cwdopen
.w:
    call cd_vnext
    jnc .rec
    or ax, ax
    jz .cda                     ; the folder's end: the tracks, at the root
    call cd_ferr                ; a READ that failed is not "no such name"
    jmp short .out
.rec:
    call cd_mangle
    push si
    mov si, cd_nm
    call cd_wantis
    pop si
    jne .w
    mov cx, [si+CDR_SIZE]
    mov dx, [si+CDR_SIZE+2]
    xor bl, bl
    test byte [si+CDR_FLAGS], 2
    jz .found
    mov bl, 0x10                ; a folder: size 0, as its listing says
    xor cx, cx
    xor dx, dx
.found:
    clc
    jmp short .out
.cda:
    cmp word [cd_cwd], 0
    jne .noent
    mov al, [cd_first]
.t:
    call cd_isaudio
    jc .tn
    call cd_cdaname
    mov si, cd_nm
    call cd_wantis
    je .tfound
.tn:
    inc al
    cmp al, [cd_last]
    jbe .t
.noent:
    mov ax, FERR_NOENT
    stc
    jmp short .out
.tfound:
    xor ah, ah
    add ax, CD_HCDA
    mov cx, 44
    xor dx, dx
    xor bl, bl
    clc
.out:
    pop di
    pop si
    ret

; cd_wantis - SI = a name in our DS -> ZF = 1 it is cd_want. clobbers: flags
cd_wantis:
    push ax
    push si
    push di
    mov di, cd_want
.c:
    mov al, [si]
    cmp al, [di]
    jne .out
    inc si
    inc di
    or al, al
    jnz .c
.out:
    pop di
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; cd_open - the start shared by FSV_READ and FSV_READAT: the destination
;           banked, the disc checked, AX's handle resolved and refused if it is
;           a folder. in: AX = a handle, DX:BX = the buffer
; out: CF = 0; CF = 1 and AX = a FERR_* (DX = 0)
; -----------------------------------------------------------------------------
cd_open:
    mov [cd_bseg], dx
    mov [cd_boff], bx
    push ax
    call cd_media
    jnc .known
    pop dx
    jmp cd_ferr
.known:
    pop ax
    cmp byte [cd_stale], 0
    jne .noent
    call cd_resolve
    jc .fail
    test byte [cd_r_fl], 2
    jz .out                     ; CF = 0 off the `test`
    mov ax, FERR_PROT           ; a folder is not a file to read
    jmp short .fail
.noent:
    mov ax, FERR_NOENT
.fail:
    xor dx, dx
    stc
.out:
    ret

; -----------------------------------------------------------------------------
; FSV_READ - AX = a handle, DX:BX = the buffer, DI:CX = its capacity
; out: CF = 0 and DX:AX = the bytes read; CF = 1 and AX = a FERR_*
;
; The capacity is checked HERE, for rd_read's reason: the loader's path
; reaches this cell without passing the kernel's own check (62.9.18).
; -----------------------------------------------------------------------------
cd_read:
    push bx
    push cx
    push si
    push di
    call cd_open
    jc .out
    mov ax, [cd_r_len+2]
    cmp ax, di
    ja .big
    jb .fits
    mov ax, [cd_r_len]
    cmp ax, cx
    ja .big
.fits:
    xor ax, ax
    mov [cd_pos], ax
    mov [cd_pos+2], ax
    mov ax, [cd_r_len]
    mov [cd_left], ax
    mov ax, [cd_r_len+2]
    mov [cd_left+2], ax
    mov byte [cd_prog], 1       ; the bar is ours to step (SPEC.md 12.8.1)
    call cd_bytes
    jc .io
    mov ax, [cd_r_len]
    mov dx, [cd_r_len+2]
    jmp short .out
.big:
    mov ax, FERR_BIG
    xor dx, dx
    stc
    jmp short .out
.io:
    call cd_ferr
.out:
    pop di
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; FSV_READAT - AX = a handle, DX:BX = the buffer, CX = its capacity, DI:SI =
;              the offset. out: CF = 0 and DX:AX = the bytes delivered, 0 at
;              or past the end
; -----------------------------------------------------------------------------
cd_readat:
    push bx
    push cx
    push si
    push di
    call cd_open
    jc .out
    mov [cd_pos], si
    mov [cd_pos+2], di
    mov ax, [cd_r_len]          ; what is left past the offset, 32 bits
    mov dx, [cd_r_len+2]
    sub ax, si
    sbb dx, di
    jc .eof
    or dx, dx
    jnz .clamp
    or ax, ax
    jz .eof
    cmp ax, cx
    jbe .take
.clamp:
    mov ax, cx
.take:
    mov [cd_left], ax
    mov word [cd_left+2], 0
    mov byte [cd_prog], 0       ; the copy engine steps its own bar
    push ax
    call cd_bytes
    pop dx
    jc .io
    xchg ax, dx                 ; AX = the count
    jmp short .zhi
.eof:
    xor ax, ax
.zhi:
    xor dx, dx                  ; ...which also leaves CF = 0
    jmp short .out
.io:
    call cd_ferr
.out:
    pop di
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; cd_bytes - [cd_left] bytes of the resolved file, from [cd_pos], to
;            [cd_bseg]:[cd_boff] (119.3.4)
; out: CF = 0; CF = 1 and AX = a CDE_*
; clobbers: AX
;
; A run of whole sectors goes straight into the buffer, CD_RUN to a READ (10);
; a partial first or last sector goes through cd_sbuf, whose tag means the
; next chunk's head is not read twice. The destination is normalised after
; every piece, so a run never wraps its segment.
; -----------------------------------------------------------------------------
cd_bytes:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call cd_bnorm
.loop:
    mov ax, [cd_left]
    or ax, [cd_left+2]
    jz .done
    cmp byte [cd_r_cda], 0
    jne .cda
    mov ax, [cd_pos]            ; the sector: the extent + pos / 2,048...
    mov dx, [cd_pos+2]
    mov si, ax
    and si, 2047                ; ...and the offset into it
    mov cl, 11
    shr ax, cl
    mov bx, dx
    mov cl, 5
    shl bx, cl
    or ax, bx
    mov cl, 11
    shr dx, cl
    add ax, [cd_r_lba]
    adc dx, [cd_r_lba+2]
    or si, si
    jnz .part
    cmp word [cd_left+2], 0
    jne .whole
    cmp word [cd_left], 2048
    jb .part
.whole:                         ; whole sectors, straight in
    mov cx, CD_RUN
    cmp word [cd_left+2], 0
    jne .k
    mov bx, [cd_left]
    push cx
    mov cl, 11
    shr bx, cl
    pop cx
    cmp bx, cx
    jae .k
    mov cx, bx
.k:
    mov bx, [cd_bseg]
    mov [cd_xseg], bx
    mov bx, [cd_boff]
    mov [cd_xoff], bx
    call cd_read10
    jc .out
    mov ax, cx
    mov cl, 11
    shl ax, cl                  ; the bytes it moved
    jmp short .adv
.part:                          ; a piece of one sector, through the buffer
    call cd_rdsec
    jc .out
    mov ax, 2048
    sub ax, si
    cmp word [cd_left+2], 0
    jne .copy
    cmp ax, [cd_left]
    jbe .copy
    mov ax, [cd_left]
.copy:
    add si, cd_sbuf
.copyfrom:                      ; AX bytes from DS:SI to the destination
    mov cx, ax
    mov es, [cd_bseg]
    mov di, [cd_boff]
    cld
    rep movsb
.adv:                           ; AX = the bytes this piece moved
    add [cd_pos], ax
    adc word [cd_pos+2], 0
    sub [cd_left], ax
    sbb word [cd_left+2], 0
    add [cd_boff], ax
    call cd_bnorm
    cmp byte [cd_prog], 0
    je .loop
    call OSAPI_FS_PROG          ; AX = since the last report; it keeps every
    jmp .loop             ; register and the flags
.cda:                           ; a TRACKnn.CDA: composed, then copied
    call cd_mkcda               ; (the callers clamp to its 44 bytes)
    mov si, [cd_pos]
    add si, cd_cda
    mov ax, [cd_left]
    jmp short .copyfrom
.done:
    clc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; cd_bnorm - [cd_bseg]:[cd_boff] with the offset under 16. clobbers: nothing
cd_bnorm:
    push ax
    push cx
    mov ax, [cd_boff]
    mov cl, 4
    shr ax, cl
    add [cd_bseg], ax
    and word [cd_boff], 15
    pop cx
    pop ax
    ret
