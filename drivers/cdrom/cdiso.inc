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
; cd_sopen - SI = an extent then its length (two dwords): the walk starts at
;            its first record. clobbers: AX, BX, CX, DX
; cd_dopen - DX:AX = an extent, CX:BX = its length in bytes: the same.
;            clobbers: AX, BX, CX, DX
;
; NEITHER BANKS A REGISTER: cd_cwdopen banks all four for its callers, and
; cd_parent and cd_enum hold nothing in them across the call.
; -----------------------------------------------------------------------------
cd_sopen:
    mov ax, [si]
    mov dx, [si+2]
    mov bx, [si+4]
    mov cx, [si+6]
cd_dopen:
    mov [cd_it_lba], ax
    mov [cd_it_lba+2], dx
    xchg ax, bx                 ; DX:AX = the length (CX:BX is spent)
    xchg dx, cx
    add ax, 2047
    adc dx, 0
    mov cl, 11
    shr ax, cl
    mov cl, 5
    shl dx, cl
    or ax, dx                   ; sectors: a directory is far under 64K of them
    mov [cd_it_n], ax
    xor ax, ax
    mov [cd_it_off], ax
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
    add si, cd_sbuf             ; CF = 0: the sum is far inside the segment
    jmp short .out
.nextsec:
    dec word [cd_it_n]
    add word [cd_it_lba], 1
    adc word [cd_it_lba+2], 0
    and word [cd_it_off], 0
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
    mov al, [si+CDR_LEN]        ; a name that runs past its record is no
    sub al, CDR_NAME            ; record: listed it would be read from the
    cmp al, [si+CDR_NLEN]       ; bytes after it (the next record, or past
    jb cd_vnext                 ; cd_sbuf's end). A record is at least 34
                                ; bytes (cd_dnext), so length - 33 is 1..222
                                ; and the compare cannot wrap a byte
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

; cd_upper - AL -> upper case. clobbers: nothing
cd_upper:
    cmp al, 'a'
    jb .out
    cmp al, 'z'
    ja .out
    sub al, 'a' - 'A'
.out:
    ret

; cd_legal - AL -> upper case, and CF = 0 a FAT short-name character: A-Z,
;            0-9 and the kernel's own dskw_syms. clobbers: nothing but AL's
;            case. Every caller upper-cased first, so it is done here, once
cd_legal:
    call cd_upper
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

; -----------------------------------------------------------------------------
; cd_mangle - SI = a record, DI = where its 8.3 name goes (cd_nm, or cd_ent
;             itself) -> the name, NUL-ended
; out: DI = one past the NUL; every other register kept
;
; The ;version and trailing dots go; letters are upper-cased; a name that is
; then legal 8.3 is used as it is. Otherwise: six legal characters of the
; base, `~`, one character of the stripped name's byte sum mod 36, and three
; legal characters of the last extension behind a dot.
;
; THE REGISTERS ARE THE WHOLE STATE: SI = the name, BX = the index into it
; ([bx+si] is an 8086 address where [si+di] is not), BP = L, its stripped
; length, DX = where the base ends, CX = where the extension starts, AH a
; count, and DI the output through `stosb`. The hash rides the STACK from
; where it is computed to the one path that writes it, and the plain path
; drops it.
; -----------------------------------------------------------------------------
cd_mangle:
    push ax
    push bx
    push cx
    push dx
    push si
    push bp
    push es
    push ds
    pop es                      ; ES:DI = the output, ours
    cld
    mov cl, [si+CDR_NLEN]
    xor ch, ch
    add si, CDR_NAME            ; SI = the name, CX = its length
    xor bx, bx                  ; BX = L: up to the ;version
.scan:
    cmp bx, cx
    jae .trim
    cmp byte [bx+si], ';'
    je .trim
    inc bx
    jmp short .scan
.trim:
    or bx, bx
    jz .trimmed
    cmp byte [bx+si-1], '.'
    jne .trimmed
    dec bx
    jmp short .trim
.trimmed:
    mov bp, bx                  ; BP = L
    xor dx, dx                  ; DX = the byte sum, upper-cased
    mov cx, 0xFFFF              ; ...and CX where the last dot is
    xor bx, bx
.h:
    cmp bx, bp
    jae .hd
    mov al, [bx+si]
    call cd_upper
    xor ah, ah
    add dx, ax
    cmp al, '.'
    jne .hn
    mov cx, bx
.hn:
    inc bx
    jmp short .h
.hd:
    xchg ax, dx
    xor dx, dx
    mov bx, 36
    div bx                      ; DX = the sum mod 36
    xchg ax, dx
    add al, '0'
    cmp al, '9'
    jbe .hc
    add al, 'A' - '9' - 1
.hc:
    push ax                     ; THE HASH, until .tilde or .plain pops it
    mov dx, cx                  ; DX = where the base ends: the dot...
    inc cx                      ; ...CX = where the extension starts, past it
    jnz .split                  ; (no dot was 0FFFFh, and wraps to 0)
    mov dx, bp                  ; no dot: the base is all of it, and the
    mov cx, bp                  ; extension starts (and ends) at L
.split:
    or dx, dx                   ; a legal 8.3 name as it stands?
    jz .mangle
    cmp dx, 8
    ja .mangle
    mov ax, bp
    sub ax, cx
    cmp ax, 3
    ja .mangle
    xor bx, bx
.lb:
    cmp bx, dx
    jae .le0
    mov al, [bx+si]
    call cd_legal
    jc .mangle
    inc bx
    jmp short .lb
.le0:
    mov bx, cx
.le:
    cmp bx, bp
    jae .plain
    mov al, [bx+si]
    call cd_legal
    jc .mangle
    inc bx
    jmp short .le
.plain:                         ; the base, and the extension behind a dot
    pop ax                      ; (the hash: not wanted here)
    xor bx, bx
.pb:
    cmp bx, dx
    jae .pe0
    mov al, [bx+si]
    call cd_upper
    stosb
    inc bx
    jmp short .pb
.pe0:
    cmp cx, bp
    jae .done
    mov al, '.'
    stosb
    mov bx, cx
.pe:
    cmp bx, bp
    jae .done
    mov al, [bx+si]
    call cd_upper
    stosb
    inc bx
    jmp short .pe
.mangle:                        ; six legal characters of the base...
    xor bx, bx
    xor ah, ah                  ; AH = how many went in
.mb:
    cmp bx, dx
    jae .mbd
    cmp ah, 6
    jae .mbd
    mov al, [bx+si]
    call cd_legal
    jc .mbn
    stosb
    inc ah
.mbn:
    inc bx
    jmp short .mb
.mbd:
    or ah, ah
    jnz .tilde
    mov al, '_'                 ; ...or one that stands for none
    stosb
.tilde:
    mov al, '~'
    stosb
    pop ax                      ; the hash
    stosb
    mov bx, cx                  ; ...and three of the extension
    xor ah, ah
.me:
    cmp bx, bp
    jae .done
    cmp ah, 3
    jae .done
    mov al, [bx+si]
    call cd_legal
    jc .men
    or ah, ah
    jnz .mep
    mov byte [di], '.'
    inc di
.mep:
    stosb
    inc ah
.men:
    inc bx
    jmp short .me
.done:
    xor al, al
    stosb
    pop es
    pop bp
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

; cd_ezero - cd_ent all zero, ES left alone. clobbers: CX, DI (AX kept)
cd_ezero:
    mov di, cd_ent
    mov cx, 16
    jmp cd_wzero

; -----------------------------------------------------------------------------
; cd_stage - SI = a record, AX = its handle -> cd_ent
; clobbers: AX, BX, CX, DI - its two callers want none of them back: cd_list
;           banks every register round its walk, and cd_enum hands the entry
;           over and reloads AX itself
;
; THE NAME IS MANGLED STRAIGHT INTO cd_ent, which is what cd_mangle's DI is
; for: the name used to go to cd_nm and be copied across a byte at a time.
; -----------------------------------------------------------------------------
cd_stage:
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
    mov di, cd_ent              ; @0: the name
    call cd_mangle              ; DI = one past its NUL
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
    test byte [si+1], CDT_DATA  ; CF = 0 off the `test`, which `pop` keeps
    pop si
    jnz .no
    ret
.no:
    stc
    ret

; cd_cdaname - AL = a track, DI = 12 bytes -> 'TRACKnn.CDA'. clobbers: nothing
cd_cdaname:
    push ax
    mov word [di], 'TR'
    mov word [di+2], 'AC'
    mov byte [di+4], 'K'
    aam                         ; AH = tens, AL = units (the track is <= 99)
    xchg al, ah
    add ax, '00'
    mov [di+5], ax
    mov word [di+7], '.C'
    mov word [di+9], 'DA'
    mov byte [di+11], 0
    pop ax
    ret

; -----------------------------------------------------------------------------
; cd_stagecda - AL = an audio track -> cd_ent: TRACKnn.CDA, 44 bytes, type 0
; clobbers: CX, DI - AX is kept, being both callers' loop counter
; -----------------------------------------------------------------------------
cd_stagecda:
    call cd_ezero
    mov di, cd_ent
    call cd_cdaname             ; @0: the name, straight into the entry
    push ax
    xor ah, ah
    add ax, CD_HCDA
    mov [cd_ent+18], ax
    pop ax
    mov word [cd_ent+20], 44
    ret

; -----------------------------------------------------------------------------
; cd_mkcda - [cd_r_cda]'s 44 bytes into cd_cda (119.3.5)
; clobbers: AX, BX, CX, DX, DI - its one caller is cd_bytes, which holds none
;           of them across it (and its callers bank them all)
;
; Bytes 0-21 are cd_cda's own load-time value and are never written
; (cdrom.asm); bytes 23, 39 and 43 are 0 from the load and never written.
; -----------------------------------------------------------------------------
cd_mkcda:
    mov al, [cd_r_cda]
    mov [cd_cda+22], al         ; the track
    mov al, [cd_last]           ; the disc id: the lead-out, xor the track
    inc ax                      ; count in its top byte
    call cd_tstart
    mov cx, [cd_first]          ; CL = the first track, CH = the last
    sub ch, cl
    inc ch
    xor dh, ch
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
                                ; ...and the length's, the tail call

; cd_msfle - DX:AX frames -> [DI] = F, S, M (the fourth byte, M's pad, is the
;            caller's: both of cd_mkcda's are 0 and never written).
;            clobbers: AX, CX, DX
cd_msfle:
    xor cx, cx                  ; no lead-in to add
    call cd_mcore
    mov [di], ah
    mov [di+1], al
    mov [di+2], cl
    ret

; -----------------------------------------------------------------------------
; cd_mcore - DX:AX frames, plus CX frames of lead-in, -> CL = minutes, AL =
;            seconds, AH = frames, and DX = the frames past the minute - the
;            value the two routines this replaced left there, which CDV_PLAY
;            hands back (cd_cmd keeps DX). clobbers: CH
;
; THE TWO CLAMPS GUARD THE DIVIDE (INT 0 on an overflow, and nothing here
; handles one): a negative address is the disc's start, and a quotient past
; 4,499 minutes does not fit. The lead-in is added BETWEEN them - after the
; negative clamp, so a pregap address still reads as the disc's start plus
; two seconds, and BEFORE the high one, which is what makes the high one
; hold: cd_msf used to add its 150 after clamping, and an address at
; 4,499:FF6Ah or above carried into 4,500 and faulted (DEFECTS, dsp1/cdram.md).
; Below that the answer is the one it always was, cd_msf's and cd_msfle's.
; -----------------------------------------------------------------------------
cd_mcore:
    or dx, dx
    jns .pos
    xor ax, ax
    cwd
.pos:
    add ax, cx
    adc dx, 0
    cmp dx, 4500
    jb .ok
    mov dx, 4499
.ok:
    mov cx, 4500                ; frames a minute
    div cx                      ; AX = minutes, DX = the rest
    xchg ax, cx                 ; CL = minutes
    mov ax, dx                  ; AX = the rest
    mov ch, 75
    div ch                      ; AL = seconds, AH = frames
    ret

; =============================================================================
; RESOLVING A HANDLE (SPEC.md 119.3.3)
; =============================================================================

; -----------------------------------------------------------------------------
; cd_resolve - AX = a handle -> what it names
; out: CF = 0 and [cd_r_lba], [cd_r_len], [cd_r_fl], [cd_r_cda]; CF = 1 and
;      AX = a FERR_*
; clobbers: AX - on a refusal, and to the TRACK on a TRACKnn.CDA's handle. A
;      record's handle comes back in AX as it went in, which cd_chdir and
;      cd_enum both read straight after (DEFECTS, docs/plans/dsp1/cdram.md:
;      the copy out of the record used to go through AX)
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
    jmp .out              ; CF = 0 off the `jc`
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
    mov cl, 5                   ; (the slot's shift, the same every record)
.w:
    cmp si, 2048 - 34
    ja .noent
    mov dl, [cd_sbuf+si]
    xor dh, dh
    cmp dl, 34
    jb .noent
    mov di, si
    shr di, cl
    cmp di, bx
    je .found
    ja .noent
    add si, dx
    jmp short .w
.found:
    add si, cd_sbuf
    mov dl, [si+CDR_LEN]        ; a name that runs past the record is no
    sub dl, CDR_NAME            ; record - and the length is at least 34
    cmp dl, [si+CDR_NLEN]       ; (above), so less 33 it is 1..222 and the
    jb .noent                   ; byte compare cannot wrap
    mov dl, [si+CDR_FLAGS]
    push ax                     ; the handle, into the next cache row: the
    mov al, [cd_hcnext]         ; counter is taken MOD CD_HC_N where it is
    inc byte [cd_hcnext]        ; read, so it may simply count
    and al, CD_HC_N - 1
    mov ah, CD_HC_SZ
    mul ah
    add ax, cd_hcache
    xchg di, ax
    pop ax
    push es
    push ds
    pop es
    cld
    stosw                       ; the handle...
    inc si
    inc si
    movsw                       ; ...the extent (CDR_EXT)...
    movsw
    add si, CDR_SIZE - CDR_EXT - 4
    movsw                       ; ...the length (CDR_SIZE)...
    movsw
    pop es
    mov [di], dl                ; ...and the flags
    lea si, [di-8]              ; SI = the row's extent then length
    jmp short .take
.hit:
    mov dl, [si+10]
    add si, 2
.take:                          ; SI = extent then length, DL = the flags
    mov di, cd_r_lba            ; ...copied out by a routine that keeps AX
    mov cx, 4
    call cd_wcopy
    mov [cd_r_fl], dl           ; (CF = 0: the last arithmetic was an `add`,
    clc                         ; a `lea` or a `jmp` - said, not assumed)
    jmp short .out
.io:
    mov ax, FERR_IO
    jmp short .fail
.noent:
    mov ax, FERR_NOENT
.fail:
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
; cd_mediak - cd_media for an FSV_* verb: AX KEPT when the disc answers (ZF
;             still cd_media's stale flag), and a refusal turned into the
;             verb's own answer by cd_ferr below - FERR_*, DX = 0, CF = 1. It
;             was the same seven instructions in four verbs
; cd_ferr   - AX = a CDE_* -> the FERR_* a verb answers: an empty drive is
;             FERR_NODISK, the rest FERR_IO. CF = 1, DX = 0 (a READ's count)
; -----------------------------------------------------------------------------
cd_mediak:
    push ax
    call cd_media
    jc .no
    pop ax                      ; (`pop` keeps the flags)
    ret
.no:
    pop dx                      ; the caller's AX, dropped: AX is the CDE_*
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
    push si
    mov si, cd_cwdlba
    call cd_sopen
    pop si
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
    call cd_mediak
    jc .out
    or ax, ax
    jz .root
    cmp byte [cd_stale], 0
    jne .noent
    call cd_resolve
    jc .out
    test byte [cd_r_fl], 2
    jz .noent                   ; nothing chdirs into a file
    mov [cd_cwd], ax            ; the HANDLE: cd_resolve keeps AX (and it
    mov si, cd_r_lba            ; did not, which is DEFECTS 1 in
    call cd_setcwd              ; docs/plans/dsp1/cdram.md)
    call cd_parent              ; DX = the parent's handle
    clc
    jmp short .out
.root:
    mov byte [cd_stale], 0
    mov [cd_cwd], ax
    mov si, cd_rlba
    call cd_setcwd
    xor dx, dx                  ; the root's parent is never read - and CF = 0
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
;             clobbers: CX, SI, DI - cd_chdir, the one caller, banks them
cd_setcwd:
    mov di, cd_cwdlba
    mov cx, 4
    jmp cd_wcopy

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
    push ax                     ; (BX, CX and SI are spent too: cd_chdir, the
                                ; one caller, banks them round the call)
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
    pop ax
    ret

; cd_dotdot - DX:AX = a directory's extent -> DX:AX = its parent's, out of the
;             second record of its first sector; CF = 1 unreadable
;             clobbers: AX, DX on a refusal (cd_parent then answers the
;             root, reading neither), SI
cd_dotdot:
    call cd_rdsec
    jc .out
    mov si, cd_sbuf
    mov al, [si]
    xor ah, ah
    cmp al, 34
    jb .out                     ; CF = 1 off the compare: unreadable
    add si, ax                  ; (CF = 0: the sum is inside cd_sbuf)
    mov ax, [si+CDR_EXT]
    mov dx, [si+CDR_EXT+2]
.out:
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
    jne .out                    ; stale (cd_media's ZF)
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
    mov di, cx                  ; DI = the ordinal: nothing below spends it
    call cd_mediak              ; until the entry is staged
    jc .out
    mov si, cd_rlba
    or ax, ax
    jz .walk
    cmp byte [cd_stale], 0
    jne .noent
    call cd_resolve             ; AX kept: it is the folder's handle
    jc .out
    test byte [cd_r_fl], 2
    jz .noent
    mov si, cd_r_lba
.walk:
    push ax                     ; the folder: 0 is the root, whose tracks
    cmp byte [cd_iso], 0        ; follow its records
    je .tracks
    call cd_sopen
.rec:
    call cd_vnext
    jnc .counted
    or ax, ax
    jz .tracks                  ; the folder's end: then the root's tracks
    pop cx                      ; a READ that failed: an I/O error, and NOT
    call cd_ferr                ; "past the end" - a copy would stop early and
    jmp short .out              ; call that success (SPEC.md 62.9.7)
.counted:
    sub di, 1                   ; this one, or one fewer to step over: the
    jnc .rec                    ; borrow is "the ordinal was 0"
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
    sub di, 1
    jc .cda
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
    les di, [cd_boff]           ; [cd_bseg]:[cd_boff], one load
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
    call cd_mediak
    jc .out
    jne .noent                  ; stale (cd_media's ZF)
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
    mov di, cd_nm
    call cd_mangle
    call cd_wantis
    jne .w
    mov cx, [si+CDR_SIZE]
    mov dx, [si+CDR_SIZE+2]
    xor bl, bl
    test byte [si+CDR_FLAGS], 2
    jz .out                     ; CF = 0 off the `test`
    mov bl, 0x10                ; a folder: size 0, as its listing says
    xor cx, cx
    xor dx, dx                  ; (CF = 0)
    jmp short .out
.cda:
    cmp word [cd_cwd], 0
    jne .noent
    mov al, [cd_first]
    mov di, cd_nm
.t:
    call cd_isaudio
    jc .tn
    call cd_cdaname
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
    xor bl, bl                  ; (CF = 0)
.out:
    pop di
    pop si
    ret

; cd_wantis - ZF = 1 cd_nm is cd_want. clobbers: flags
cd_wantis:
    push ax
    push si
    push di
    mov si, cd_nm
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
    call cd_mediak
    jc .out
    jne .noent                  ; stale (cd_media's ZF)
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
    jc cd_read.out
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
    jc cd_read.io               ; cd_read's tail, which is this one's too:
    xchg ax, dx                 ; AX = the count
    jmp short .zhi              ; the same frame, the same refusal
.eof:
    xor ax, ax
.zhi:
    xor dx, dx                  ; ...which also leaves CF = 0
    jmp short cd_read.out

; -----------------------------------------------------------------------------
; cd_bytes - [cd_left] bytes of the resolved file, from [cd_pos], to
;            [cd_bseg]:[cd_boff] (119.3.4)
; out: CF = 0; CF = 1 and AX = a CDE_*
; clobbers: AX, BX, CX, DX, SI, DI - cd_read and cd_readat, the two callers,
;           bank BX, CX, SI and DI round it and write DX after it
;
; A run of whole sectors goes straight into the buffer, CD_RUN to a READ (10);
; a partial first or last sector goes through cd_sbuf, whose tag means the
; next chunk's head is not read twice. The destination is normalised after
; every piece, so a run never wraps its segment.
; -----------------------------------------------------------------------------
cd_bytes:
    push es                     ; (BX, CX, DX, SI and DI are spent: its two
    call cd_bnorm               ; callers bank or overwrite every one)
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
    shr dx, cl                  ; (the high word's share, while CL is 11)
    mov cl, 5
    shl bx, cl
    or ax, bx
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
    les di, [cd_boff]           ; [cd_bseg]:[cd_boff], one load
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
    jmp .loop                   ; register and the flags
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
    ret

; cd_bnorm - [cd_bseg]:[cd_boff] with the offset under 128. clobbers: nothing
;
; IN WHOLE 128-BYTE GRANULES, so the segment moves in steps of 8 paragraphs:
; under kern_mega's protected mode (CPU_F_PM, ASKED here and not banked: a
; driver SYSTEM.CFG wants attaches before mega_enter, so a byte banked there
; said real mode all session - KERN-MEGA-PLAN 14.112) only a segment on that grid is
; an identity selector for the memory it names, and anything else faults or
; silently addresses some other memory (KERN-MEGA-PLAN 3.1, 14.19). The
; kernel's own dskw_norm does the same there, and its drv_fs_hi bounces a
; buffer above 1MB through dsk_secbuf, so a selector with no paragraphs never
; reaches this - the folded offset plus a CD_RUN read still cannot carry
; -----------------------------------------------------------------------------
cd_bnorm:
    push ax
    call OSAPI_CPU_INFO         ; AH = the flags NOW
    test ah, CPU_F_PM
    jz .fold
    test byte [cd_bseg], 4      ; ...and an LDT selector has a base that is no
    jnz .out                    ; paragraph at all: its offset walks, the
.fold:                          ; kernel handing it over with the read inside
    push cx                     ; its 64KB (KERN-MEGA-MEMORY-PLAN MC8)
    mov ax, [cd_boff]
    mov cl, 4
    shr ax, cl
    and al, 0xF8                ; whole granules only
    add [cd_bseg], ax
    and word [cd_boff], 127
    pop cx
.out:
    pop ax
    ret
