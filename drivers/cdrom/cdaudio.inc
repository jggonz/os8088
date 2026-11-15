; =============================================================================
; os8088 - CDROM.DRV / cdaudio.inc - the package door (SPEC.md 119.4)
;
; CD audio is the DRIVE's: PLAY AUDIO MSF starts it, the drive turns the
; track into sound at its own jack and its own cable, and READ SUB-CHANNEL
; says where it has got to. Nothing here streams, decodes or mixes a byte.
;
; Every verb is UI-task only (cdpkg.inc): each one waits on the drive.
; ES IS THE CALLING PACKAGE'S SEGMENT here and nowhere else in the driver
; (SPEC.md 20.11) - CDV_DRIVE writes the model through it.
; =============================================================================

cd_pktab:
    dw cd_v_ident, cd_v_drive, cd_v_disc, cd_v_track, cd_v_play
    dw cd_v_pause, cd_v_resume, cd_v_stop, cd_v_status, cd_v_eject
    dw cd_v_volume
%if ($ - cd_pktab) / 2 != CDV_MAX + 1
  %error "cdrom: cd_pktab is one word per CDV_* verb"
%endif

; -----------------------------------------------------------------------------
; cd_pkg - DSV_PKGCALL: BL = a CDV_*, BH = DRVC_CD, ES = the caller's segment
; A NEAR proc with a near `ret`, like every callback here (SPEC.md 51.2).
; -----------------------------------------------------------------------------
cd_pkg:
    cmp bl, DRV_ICONV
    je cd_v_icon                ; the kernel's, not a package's: the volume's
                                ; desktop picture (cdico.inc, SPEC.md 26.4.2)
    cmp bl, CDV_MAX
    ja .none                    ; an unchecked verb into a jump table is a
    push bx                     ; wild near call
    xor bh, bh
    shl bx, 1
    mov bp, [cd_pktab+bx]       ; (DS-relative: no BP-based address here)
    pop bx
    jmp bp
.none:
    mov ax, CDE_ARG
    stc
    ret

; --- CDV_IDENT ---------------------------------------------------------------
cd_v_ident:
    mov ax, CD_SIG
    mov cx, CD_ABI
    clc
    ret

; --- CDV_DRIVE: ES:DI = CD_MODELSZ bytes or DI = 0 ---------------------------
cd_v_drive:
    or di, di
    jz .regs
    push si
    push di
    push cx
    mov si, cd_model
    mov cx, CD_MODELSZ
    cld
    rep movsb                   ; DS:SI (ours) -> ES:DI (the caller's)
    pop cx
    pop di
    pop si
.regs:
    mov ax, [cd_base]
    mov cl, [cd_unit]
    mov dl, [cd_vol]
    clc
    ret

; --- CDV_DISC ----------------------------------------------------------------
cd_v_disc:
    call cd_media
    jc .out
    mov al, [cd_last]           ; the disc's length: the lead-out, in seconds
    inc ax
    call cd_tstart
    call cd_secs
    mov cx, ax
    mov ax, [cd_first]          ; AL = the first track, AH = the last
    mov dx, [cd_kind]           ; DL = the kinds, DH = the generation
    clc
.out:
    ret

; --- CDV_TRACK: AL = a track -------------------------------------------------
cd_v_track:
    push bx
    xchg ax, bx                 ; BL = the track: cd_media keeps BX, and AX is
    call cd_media               ; the CDE_* on a refusal
    jc .out
    mov al, bl
    cmp al, [cd_first]
    jb .arg
    cmp al, [cd_last]
    ja .arg
    call cd_tdesc               ; (SI is an output: nothing to keep)
    mov bl, [si+1]
    and bl, 0x0F
    xor bh, bh
    mov si, bx                  ; SI = the control nibble
    push ax
    call cd_tend                ; the track's end (SPEC.md 119.3.1: a CD-Extra
    mov cx, ax                  ; disc's session gap is not the track's)...
    mov bx, dx
    pop ax
    call cd_tstart              ; ...less its start: the length
    push ax
    push dx
    sub cx, ax
    sbb bx, dx
    xchg ax, cx                 ; DX:AX = the length (CX and BX are spent:
    xchg dx, bx                 ; CX is the answer, BX the frame's)
    call cd_secs
    mov cx, ax
    pop dx
    pop ax                      ; DX:AX = the start
    clc
    jmp short .out
.arg:
    mov ax, CDE_ARG
    stc
.out:
    pop bx
    ret

; -----------------------------------------------------------------------------
; CDV_PLAY - AL = first track, AH = last (0 = the last audio track), CX =
;            seconds into the first (SPEC.md 119.4)
;
; The end is the start of the track after the last - or the lead-out - and
; earlier if a data track comes first: a drive asked to play across a data
; track refuses the whole command.
; -----------------------------------------------------------------------------
cd_v_play:
    push bx
    push cx
    push si
    push di
    xchg ax, bx                 ; BL = first, BH = last: cd_media keeps BX
    call cd_media               ; and CX
    jc .out
    mov al, bl
    call cd_isaudio
    jc .arg
    or bh, bh
    jnz .lastok
    mov bh, [cd_last]           ; 0: the disc's last track - the scan below
.lastok:                        ; stops at the first data track anyway
    cmp bh, bl
    jb .arg
    cmp bh, [cd_last]
    ja .arg
    mov al, bl                  ; the end: the first data track after the
.scan:                          ; first, or the track after the last
    inc al
    cmp al, bh
    ja .endat
    call cd_isaudio
    jnc .scan
.endat:
    dec al                      ; the last track played...
    call cd_tend                ; ...and where it ends: BP:SI, the end LBA.
    xchg si, ax                 ; BP and not memory, because the door saves
    mov bp, dx                  ; BP round every verb (drv_pkg_call_x) and
    mov al, bl                  ; cd_pkg spends it on the dispatch anyway
    call cd_tstart              ; the start, plus CX seconds
    push ax
    push dx
    mov ax, 75
    mul cx                      ; DX:AX = the offset in frames
    pop cx
    pop bx
    add ax, bx
    adc dx, cx
    cmp dx, bp                  ; never at or past the end
    jb .startok
    ja .arg
    cmp ax, si
    jae .arg
.startok:
    call cd_pclear              ; (it keeps AX, and DX)
    mov byte [di], 0x47         ; PLAY AUDIO MSF
    add di, 3
    call cd_msf                 ; bytes 3-5: the start
    xchg ax, si
    mov dx, bp
    sub ax, 1                   ; the end MSF is INCLUSIVE: the last frame
    sbb dx, 0
    add di, 3
    call cd_msf                 ; bytes 6-8: the end
    call cd_cmd
    jmp short .out
.arg:
    mov ax, CDE_ARG
    stc
.out:
    pop di
    pop si
    pop cx
    pop bx
    ret

; cd_msf - DX:AX = an LBA -> [DI] = M, S, F (MSF counts the 150-frame
;          lead-in, which cd_mcore adds between its two clamps).
;          clobbers: AX, CX, DX
cd_msf:
    mov cx, 150
    call cd_mcore
    mov [di], cl                ; M...
    mov [di+1], ax              ; ...then S and F, AL and AH
    ret

; --- CDV_PAUSE / CDV_RESUME: PAUSE/RESUME (4Bh), byte 8 bit 0 = resume --------
cd_v_pause:
    xor al, al
    jmp short cd_pausebit
cd_v_resume:
    mov al, 1
cd_pausebit:
    push di
    push cx
    call cd_pclear              ; (it keeps AL)
    mov byte [di], 0x4B
    mov [di+8], al
    call cd_cmd
    pop cx
    pop di
    ret

; --- CDV_STOP: STOP PLAY/SCAN (4Eh), or a pause where a drive has none -------
cd_v_stop:
    push di
    push cx
    call cd_pclear
    mov byte [di], 0x4E
    call cd_cmd
    pop cx
    pop di
    jnc .out
    cmp ax, CDE_NOAUDIO
    je cd_v_pause               ; MMC-1 drives: a pause is a stop
.out:                           ; ...and an empty drive is not a reason to
    ret                         ; pause: CF = 1 still, every other CDE_* being
                                ; below CDE_NOAUDIO
%if CDE_NOAUDIO < CDE_NODISC || CDE_NOAUDIO < CDE_NOTREADY || CDE_NOAUDIO < CDE_IO || CDE_NOAUDIO < CDE_ARG
  %error "cdrom: cd_v_stop's refusal keeps the compare's borrow, so CDE_NOAUDIO is the largest CDE_*"
%endif

; -----------------------------------------------------------------------------
; CDV_STATUS - READ SUB-CHANNEL (42h), current position (SPEC.md 119.4)
; out: AL = CDS_*, AH = the track, CX = seconds into it, DX = into the disc,
;      SI = the media generation
; -----------------------------------------------------------------------------
cd_v_status:
    push bx
    push di
    call cd_pclear
    mov byte [di], 0x42
    mov word [di+2], 0x0140     ; SubQ (40h), and format 1: the position
    mov byte [di+8], 16         ; allocation length
    mov si, cd_abuf
    mov cx, 16
    call cd_xin
    mov byte [cd_abuf+1], 0
    call cd_cmd
    jc .out
    cmp word [cd_xgot], 16
    jae .full
    mov ax, CDE_IO              ; a short answer: the bytes past it are an
    stc                         ; earlier command's, and not a position
    jmp short .out
.full:
    mov si, cd_abuf + 12        ; the track-relative address: negative in a
    call cd_be32                ; pregap, which shows as 0 - cd_secs's own
    call cd_secs                ; clamp, which the 0 it used to be set to
                                ; came out of anyway
    mov cx, ax
    mov si, cd_abuf + 8         ; the absolute address
    call cd_be32
    call cd_secs
    mov dx, ax
    mov al, [cd_abuf+1]         ; the audio status, 11h..15h:
    sub al, 0x11                ; 11h playing .. 14h error = 0 .. 3 here,
    cmp al, 4                   ; then 1 .. 4 - and CDS_IDLE for 15h and
    jb .st                      ; anything unknown, through 0FFh
    mov al, 0xFF
.st:
    inc ax                      ; (AH is written next)
    mov ah, [cd_abuf+6]
    mov bl, [cd_gen]
    xor bh, bh
    mov si, bx
    clc
.out:
    pop di
    pop bx
    ret
%if CDS_PLAYING != 1 || CDS_PAUSED != 2 || CDS_DONE != 3 || CDS_ERROR != 4
  %error "cdrom: CDV_STATUS maps the drive's 11h..14h onto CDS_* by subtraction"
%endif

; --- CDV_EJECT: AL = 0 open, 1 close -----------------------------------------
cd_v_eject:
    push cx
    push di
    push ax
    call cd_pclear
    mov byte [di], 0x1E         ; PREVENT ALLOW MEDIUM REMOVAL: allow
    call cd_cmd                 ; (a refusal here is answered by the next one)
    call cd_pclear
    pop ax
    and al, 1
    or al, 2                    ; LoEj, and Start to close
    mov byte [di], 0x1B         ; START STOP UNIT
    mov [di+4], al
    call cd_forget              ; whatever was in the drive is not now
    call cd_cmd
    pop di
    pop cx
    ret

; -----------------------------------------------------------------------------
; CDV_VOLUME - AL = 0 get / 1 set (CL/CH = left/right): mode page 0Eh's port
;              0 and port 1 levels, through MODE SENSE (10) and MODE SELECT
;              (10). out CL/CH = what the drive holds
; -----------------------------------------------------------------------------
CD_MPLEN     equ 32             ; MODE SENSE's allocation: the header (8),
                                ; a block descriptor if the drive sends one
                                ; (8), and page 0Eh (16)
cd_v_volume:
    push bx
    push si
    push di
    push ax
    push cx
    call cd_vsense
    pop cx
    pop bx                      ; BL = get / set
    jc .out
    or bl, bl
    jz .get                     ; SI = the page, out of cd_vsense
    mov [si+9], cl              ; port 0's volume...
    mov [si+11], ch             ; ...and port 1's
    and byte [si], 0x3F         ; PS is not ours to send back
    mov word [cd_abuf], 0       ; the mode data length is reserved in SELECT
    call cd_pclear
    mov word [di], 0x1055       ; MODE SELECT (10), and PF
    mov cx, si                  ; what goes back: the header, the descriptor
    sub cx, cd_abuf - 16        ; if any, and the page - as it came
    mov [di+8], cl
    mov si, cd_abuf
    call cd_xin
    mov byte [cd_xout], 1
    call cd_cmd
    jc .out
    call cd_vsense              ; ...and what it holds now
    jc .out
.get:                           ; SI = the page, out of either cd_vsense
    mov cl, [si+9]
    mov ch, [si+11]
    clc
.out:
    pop di
    pop si
    pop bx
    ret

; cd_vsense - MODE SENSE (10), page 0Eh, into cd_abuf; out CF = 0 and SI =
;             the page (past the header and any block descriptor).
;             clobbers: AX, CX, SI, DI
cd_vsense:
    call cd_pclear
    mov byte [di], 0x5A         ; MODE SENSE (10)
    mov byte [di+2], 0x0E       ; the CD audio control page, current values
    mov byte [di+8], CD_MPLEN
    mov si, cd_abuf
    mov cx, CD_MPLEN
    call cd_xin
    call cd_cmd
    jc .out
    mov ax, [cd_abuf+6]         ; the block descriptors' length, big-endian
    xchg al, ah
    cmp ax, 8
    ja .short                   ; more than one descriptor: not one we follow
    add ax, cd_abuf + 8
    mov si, ax
    sub ax, cd_abuf - 12        ; the page must have arrived: 12 bytes of it
    cmp ax, [cd_xgot]
    ja .short
    mov al, [si]
    and al, 0x3F
    cmp al, 0x0E
    je .out                     ; CF = 0 off the compare
.short:
    mov ax, CDE_NOAUDIO         ; no audio control page: no volume to set
    stc
.out:
    ret
