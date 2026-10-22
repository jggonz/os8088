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
    mov ax, [cd_xgot]
    cmp ax, [cd_xmax]
    mov ax, CDE_IO              ; a SHORT read is a failed one: the buffer
    jne .short                  ; holds old bytes past it, and cd_rdsec would
                                ; tag them as the sector asked for
    pop ax                      ; the LBA back, for cd_rdsec's tag
    pop dx
    jmp short .out              ; (CF = 0: `pop` writes no flag)
.short:
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
%ifdef CDSIM_AUDIO
    call cd_simtoc              ; the gate's fake audio tracks (below)
    mov al, [cd_toc+2]
    mov ah, [cd_toc+3]
%endif
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
    or dx, dx
    js .zero                    ; before the disc's start (a pregap, or a drive
                                ; that reports FFFFFF6Ah idle): 0
    cmp dx, 75
    jae .max                    ; the quotient would not fit a word: a `div`
    push cx                     ; that overflows is INT 0, and nothing here
    mov cx, 75                  ; handles one - it would re-run for ever
    div cx
    pop cx
    ret
.zero:
    xor ax, ax
    xor dx, dx
    ret
.max:
    mov ax, 0xFFFF
    xor dx, dx
    ret

; -----------------------------------------------------------------------------
; cd_tend - AL = a track -> DX:AX = the LBA one past its last frame
; clobbers: nothing else
;
; The next track's start, or the lead-out - LESS THE SESSION GAP when this is
; an audio track and the next is data: a CD-Extra disc's data track is its
; second session, and between the two lie the first session's lead-out, the
; second's lead-in and a pregap, 11,400 frames that are not this track's
; (Blue Book). Never before the track's own start, whatever the TOC says.
; -----------------------------------------------------------------------------
CD_SGAP      equ 6750 + 4500 + 150
cd_tend:
    push bx
    push cx
    push si
    mov bl, al                  ; this track
    call cd_tstart
    mov cx, ax                  ; CX:SI... its start, kept to clamp against
    mov si, dx
    push si
    push cx
    mov al, bl
    inc al
    call cd_tstart              ; DX:AX = the next start, or the lead-out
    cmp bl, [cd_last]
    jae .clamp                  ; the last track: the lead-out is its end
    push ax
    push si
    mov al, bl
    call cd_tdesc
    mov bh, [si+1]              ; this track's control...
    mov al, bl
    inc al
    call cd_tdesc
    mov cl, [si+1]              ; ...and the next's
    pop si
    pop ax
    test bh, CDT_DATA
    jnz .clamp
    test cl, CDT_DATA
    jz .clamp
    sub ax, CD_SGAP             ; audio, then data: the session gap is not ours
    sbb dx, 0
.clamp:
    pop cx                      ; CX = the start's low word...
    pop si                      ; ...SI its high
    cmp dx, si
    jg .out
    jl .start
    cmp ax, cx
    ja .out
.start:
    mov ax, cx                  ; a TOC that runs backwards: the track is empty
    mov dx, si
.out:
    pop si
    pop cx
    pop bx
    ret

%ifdef CDSIM_AUDIO
; -----------------------------------------------------------------------------
; cd_simtoc - TEST BUILD ONLY (`make cdromtest`'s build/cdromsim.drv, never
; shipped): a one-track data disc - which is all QEMU's drive can hold - is
; given three AUDIO tracks after it, 40, 30 and 20 seconds, so the gate can
; drive TRACKnn.CDA, its association and CDRack's display on QEMU
; (SPEC.md 119.7). PLAY AUDIO is still the drive's, and QEMU refuses it.
; clobbers: AX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
cd_simtoc:
    cmp word [cd_toc+2], 0x0101 ; one track, and it is track 1
    jne .out
    mov si, cd_toc + 4 + 8 + 4  ; the lead-out's LBA: the audio's start
    call cd_be32
    mov di, cd_toc + 4 + 8      ; descriptors 2, 3, 4 and the new lead-out
    mov cx, 4
    mov bx, cd_simlen
.t:
    mov byte [di], 0
    mov byte [di+1], 0x10       ; ADR 1, control 0: audio
    mov byte [di+2], 6          ; track 2, 3, 4, then AAh (below)
    sub [di+2], cl
    mov byte [di+3], 0
    push di
    add di, 4
    call cd_pbe32
    pop di
    add ax, [bx]
    adc dx, 0
    add bx, 2
    add di, 8
    loop .t
    mov byte [di-8+2], 0xAA     ; the last is the lead-out
    mov byte [cd_toc+3], 4
    mov word [cd_xgot], 4 + 8 * 5
.out:
    ret
cd_simlen:  dw 40 * 75, 30 * 75, 20 * 75, 0
%endif
