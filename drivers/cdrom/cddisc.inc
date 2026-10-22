; =============================================================================
; os8088 - CDROM.DRV / cddisc.inc - what is in the drive (SPEC.md 119.3.1)
;
; The TOC, the last data track's primary volume descriptor, and the path
; table's lowest directory extent - read once per disc, when a verb first
; finds cd_valid clear, and forgotten by cd_changed / cd_forget.
; =============================================================================

CD_TOCSZ     equ 4 + 8 * 100    ; READ TOC: a header and 99 tracks + lead-out
CD_VDMAX     equ 16             ; volume descriptors read before giving up
CD_PTMAX     equ 32             ; path table sectors walked (64 KB)

; -----------------------------------------------------------------------------
; cd_be32 - DS:SI -> DX:AX, a big-endian dword (internal). clobbers: nothing
; cd_pbe32 - DX:AX -> DS:DI, big-endian (internal). clobbers: nothing
; -----------------------------------------------------------------------------
cd_be32:
    mov dh, [si]
    mov dl, [si+1]
    mov ah, [si+2]
    mov al, [si+3]
    ret
cd_pbe32:
    mov [di], dh
    mov [di+1], dl
    mov [di+2], ah
    mov [di+3], al
    ret

; -----------------------------------------------------------------------------
; cd_read10 - READ (10): CX sectors from DX:AX into [cd_xseg]:[cd_xoff]
; out: CF = 0; CF = 1 and AX = a CDE_*. clobbers: AX
; -----------------------------------------------------------------------------
cd_read10:
    push cx
    push di
    push dx
    push ax
    push cx
    call cd_pclear              ; (it zeroes [cd_xmax]: set it after)
    pop cx
    mov byte [di], 0x28
    pop ax
    pop dx
    push dx
    push ax
    add di, 2
    call cd_pbe32               ; bytes 2-5: the LBA
    mov [cd_pkt+7], ch          ; bytes 7-8: the count
    mov [cd_pkt+8], cl
    mov ax, cx
    mov cl, 11
    shl ax, cl                  ; bytes: at most CD_RUN x 2,048 = 32 KB
    mov [cd_xmax], ax
    call cd_cmd
    jc .err
    pop ax                      ; the LBA back, for cd_rdsec's tag
    pop dx
    jmp short .out              ; (CF = 0: `pop` writes no flag)
.err:
    pop di                      ; the banked AX, dropped: AX is the CDE_*
    pop dx
    stc
.out:
    pop di
    pop cx
    ret

; -----------------------------------------------------------------------------
; cd_rdsec - DX:AX = an LBA, into cd_sbuf unless it is there already
; out: CF = 0; CF = 1 and AX = a CDE_*. clobbers: AX on a refusal only
; -----------------------------------------------------------------------------
cd_rdsec:
    cmp ax, [cd_stag]
    jne .read
    cmp dx, [cd_stag+2]
    je .hit
.read:
    push cx
    mov word [cd_stag+2], 0xFFFF    ; nothing, until the read is whole
    mov [cd_xseg], ds
    mov word [cd_xoff], cd_sbuf
    mov cx, 1
    call cd_read10
    jc .out
    mov [cd_stag], ax
    mov [cd_stag+2], dx
.out:
    pop cx
    ret
.hit:
    clc
    ret

; -----------------------------------------------------------------------------
; cd_media - make sure the tables describe the disc in the drive
; out: CF = 0 they do; CF = 1 and AX = CDE_* (CDE_NODISC: the drive is empty)
; clobbers: AX
;
; TEST UNIT READY every time: it touches no medium, and it is what turns a
; changed disc into UNIT ATTENTION while the old tables would still answer.
; -----------------------------------------------------------------------------
cd_media:
    push cx
    push si
    push di
    call cd_pclear              ; an all-zero packet IS TEST UNIT READY
    call cd_cmd
    jc .out
    cmp byte [cd_valid], 0
    jne .out                    ; CF = 0 off the compare
    call cd_readdisc
    jc .out
    mov byte [cd_valid], 1
.out:
    pop di
    pop si
    pop cx
    ret

; -----------------------------------------------------------------------------
; cd_readdisc - the TOC, then the volume if there is a data track
; out: CF = 0; CF = 1 and AX = a CDE_* (the TOC could not be read)
; clobbers: AX, CX, SI, DI
;
; A data track that fails is not an error: the disc reads as audio only.
; -----------------------------------------------------------------------------
cd_readdisc:
    push bx
    push dx
    mov byte [cd_iso], 0
    mov byte [cd_kind], 0
    call cd_pclear
    mov byte [di], 0x43         ; READ TOC, LBA addresses, format 0
    mov byte [di+7], CD_TOCSZ >> 8
    mov byte [di+8], CD_TOCSZ & 0xFF
    mov si, cd_toc
    mov cx, CD_TOCSZ
    call cd_xin
    call cd_cmd
    jc .fail
    mov al, [cd_toc+2]
    mov ah, [cd_toc+3]
    or al, al
    jz .bad
    cmp ah, 99
    ja .bad
    cmp al, ah
    ja .bad
    mov [cd_first], al
    mov [cd_last], ah
    sub ah, al                  ; descriptors: the tracks and the lead-out
    mov al, ah
    xor ah, ah
    add ax, 2                   ; tracks + the lead-out
    mov cl, 3
    shl ax, cl
    add ax, 4                   ; 4 + 8 x (tracks + 1)
    cmp [cd_xgot], ax
    jb .bad
    ; --- the generation: a TOC that differs is a disc that differs ----------
    xchg cx, ax                 ; CX = the bytes the TOC is
    mov si, cd_toc
    xor bx, bx
    xor ah, ah
.sum:
    lodsb
    add bx, ax
    rol bx, 1
    loop .sum
    cmp byte [cd_tsok], 0
    je .first
    cmp bx, [cd_tsum]
    je .first
    inc byte [cd_gen]           ; no UNIT ATTENTION said so, the TOC did
.first:
    mov [cd_tsum], bx
    mov byte [cd_tsok], 1
    ; --- the kinds, and the last data track ---------------------------------
    mov al, [cd_first]
.trk:
    call cd_tdesc               ; SI = the descriptor
    test byte [si+1], CDT_DATA
    jz .audio
    or byte [cd_kind], CDK_DATA
    push ax
    add si, 4
    call cd_be32
    mov [cd_vdlba], ax
    mov [cd_vdlba+2], dx
    pop ax
    jmp short .nexttrk
.audio:
    or byte [cd_kind], CDK_AUDIO
.nexttrk:
    inc al
    cmp al, [cd_last]
    jbe .trk
    test byte [cd_kind], CDK_DATA
    jz .ok
    call cd_readvol             ; a volume that fails leaves cd_iso 0
.ok:
    clc
    jmp short .out
.bad:
    mov ax, CDE_IO
.fail:
    stc
.out:
    pop dx
    pop bx
    ret

; -----------------------------------------------------------------------------
; cd_tdesc - AL = a track (or [cd_last]+1 for the lead-out) -> SI = its TOC
;            descriptor (internal). clobbers: nothing
; -----------------------------------------------------------------------------
cd_tdesc:
    push ax
    push cx
    sub al, [cd_first]
    xor ah, ah
    mov cl, 3
    shl ax, cl
    add ax, cd_toc + 4
    xchg si, ax
    pop cx
    pop ax
    ret

; cd_tstart - AL = a track (or [cd_last]+1) -> DX:AX = its start LBA; the
;             other registers kept
cd_tstart:
    push si
    call cd_tdesc
    add si, 4
    call cd_be32
    pop si
    ret

; -----------------------------------------------------------------------------
; cd_readvol - the primary volume descriptor and the path table (119.3.1)
; out: [cd_iso] = 1 and the root, the base and the path table set, or
;      [cd_iso] = 0. clobbers: AX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
cd_readvol:
    mov ax, [cd_vdlba]
    mov dx, [cd_vdlba+2]
    add ax, 16
    adc dx, 0
    mov cx, CD_VDMAX
.vd:
    push ax
    push dx
    call cd_rdsec
    jc .vdbad
    cmp word [cd_sbuf+1], 'CD'
    jne .vdbad
    cmp word [cd_sbuf+3], '00'
    jne .vdbad
    cmp byte [cd_sbuf+5], '1'
    jne .vdbad
    mov al, [cd_sbuf]
    cmp al, 1
    je .pvd
    cmp al, 255
    je .vdbad                   ; the terminator, and no primary before it
    pop dx
    pop ax
    add ax, 1
    adc dx, 0
    loop .vd
    ret                         ; [cd_iso] stays 0
.vdbad:
    pop dx
    pop ax
    ret
.pvd:
    pop dx
    pop ax
    cmp word [cd_sbuf+128], 2048
    jne .out                    ; a logical block this driver cannot address
    mov ax, [cd_sbuf+156+2]     ; the root's directory record
    mov dx, [cd_sbuf+156+4]
    mov [cd_rlba], ax
    mov [cd_rlba+2], dx
    mov [cd_dbase], ax          ; ...and the base, until the path table
    mov [cd_dbase+2], dx        ; finds a lower directory
    mov ax, [cd_sbuf+156+10]
    mov [cd_rlen], ax
    mov ax, [cd_sbuf+156+12]
    mov [cd_rlen+2], ax
    mov ax, [cd_sbuf+132]       ; the path table's size...
    mov [cd_ptlen], ax
    mov ax, [cd_sbuf+134]
    mov [cd_ptlen+2], ax
    mov ax, [cd_sbuf+140]       ; ...and its type-L copy's LBA
    mov [cd_ptlba], ax
    mov ax, [cd_sbuf+142]
    mov [cd_ptlba+2], ax
    mov byte [cd_iso], 1
    call cd_ptwalk
.out:
    ret

; -----------------------------------------------------------------------------
; cd_ptwalk - the lowest directory extent on the disc, into [cd_dbase]
; clobbers: AX, CX, DX, SI, DI
;
; The path table is a byte stream across sectors and a record may straddle
; two, so it is read a BYTE at a time through cd_ptbyte, whose sector is the
; tagged buffer: the walk reads each sector once. A table past CD_PTMAX
; sectors, or one that fails to read, leaves the root's extent as the base -
; every directory below it then lists only where it encodes (119.3.3).
; -----------------------------------------------------------------------------
cd_ptwalk:
    cmp word [cd_ptlen+2], 0    ; under 64 KB: CD_PTMAX sectors
    jne .out
    xor di, di                  ; DI = the stream offset
.rec:
    cmp di, [cd_ptlen]
    jae .out
    mov cx, di
    call cd_ptbyte              ; AL = the record's name length
    jc .out
    or al, al
    jz .out
    mov bl, al                  ; (BX is the caller's: cd_readdisc banked it)
    lea cx, [di+2]              ; the extent, four bytes little-endian
    call cd_ptbyte
    jc .out
    mov [cd_tmp], al
    inc cx
    call cd_ptbyte
    jc .out
    mov [cd_tmp+1], al
    inc cx
    call cd_ptbyte
    jc .out
    mov [cd_tmp+2], al
    inc cx
    call cd_ptbyte
    jc .out
    mov [cd_tmp+3], al
    mov ax, [cd_tmp]
    mov dx, [cd_tmp+2]
    cmp dx, [cd_dbase+2]        ; lower than the base so far?
    ja .next
    jb .lower
    cmp ax, [cd_dbase]
    jae .next
.lower:
    mov [cd_dbase], ax
    mov [cd_dbase+2], dx
.next:
    mov al, bl
    xor ah, ah
    inc ax                      ; the name, padded to even
    and al, 0xFE
    add ax, 8
    add di, ax
    jnc .rec
.out:
    ret

; cd_ptbyte - CX = an offset into the path table -> AL = its byte; CF = 1 on a
;             read that failed. clobbers: AX
cd_ptbyte:
    push dx
    push si
    mov si, cx
    and si, 2047
    mov ax, cx
    push cx
    mov cl, 11
    shr ax, cl
    pop cx
    xor dx, dx
    add ax, [cd_ptlba]
    adc dx, [cd_ptlba+2]
    call cd_rdsec
    jc .out
    mov al, [cd_sbuf+si]
.out:
    pop si
    pop dx
    ret

; -----------------------------------------------------------------------------
; cd_secs - DX:AX = a count of frames (sectors) -> AX = whole seconds (75 a
;           second; a disc is at most ~80 minutes, so the quotient is a word)
; clobbers: DX
; -----------------------------------------------------------------------------
cd_secs:
    push cx
    mov cx, 75
    div cx
    pop cx
    ret
