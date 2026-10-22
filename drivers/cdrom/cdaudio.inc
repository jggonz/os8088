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
    mov al, [cd_first]
    mov ah, [cd_last]
    mov dl, [cd_kind]
    mov dh, [cd_gen]
    clc
.out:
    ret

; --- CDV_TRACK: AL = a track -------------------------------------------------
cd_v_track:
    push bx
    push ax
    call cd_media
    pop bx                      ; BL = the track (AX is the CDE_* on a refusal)
    jc .out
    mov al, bl
    cmp al, [cd_first]
    jb .arg
    cmp al, [cd_last]
    ja .arg
    push si
    call cd_tdesc
    mov bl, [si+1]
    pop si
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
    mov ax, cx
    mov dx, bx
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
    push ax
    push cx
    call cd_media
    pop cx
    pop bx                      ; BL = first, BH = last
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
    call cd_tend                ; ...and where it ends
    mov [cd_tmp2], ax           ; the end LBA
    mov [cd_tmp2+2], dx
    mov al, bl
    call cd_tstart              ; the start, plus CX seconds
    push ax
    push dx
    mov ax, 75
    mul cx                      ; DX:AX = the offset in frames
    pop cx
    pop bx
    add ax, bx
    adc dx, cx
    cmp dx, [cd_tmp2+2]         ; never at or past the end
    jb .startok
    ja .arg
    cmp ax, [cd_tmp2]
    jae .arg
.startok:
    push ax
    push dx
    call cd_pclear
    mov byte [di], 0x47         ; PLAY AUDIO MSF
    pop dx
    pop ax
    add di, 3
    call cd_msf                 ; bytes 3-5: the start
    mov ax, [cd_tmp2]
    mov dx, [cd_tmp2+2]
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
;          lead-in). clobbers: AX, DX
cd_msf:
    push cx
    or dx, dx                   ; a negative address is the disc's start, and
    jns .pos                    ; one past 74 hours does not exist: either
    xor ax, ax                  ; would overflow the divide below (INT 0)
    xor dx, dx
.pos:
    cmp dx, 4500
    jb .ok
    mov dx, 4499
.ok:
    add ax, 150
    adc dx, 0
    mov cx, 4500
    div cx                      ; AX = minutes, DX = the frames left
    mov [di], al
    mov ax, dx
    mov cl, 75
    div cl                      ; AL = seconds, AH = frames
    mov [di+1], al
    mov [di+2], ah
    pop cx
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
    push ax
    call cd_pclear
    pop ax
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
    jne .fail                   ; an empty drive is not a reason to pause
    jmp short cd_v_pause        ; MMC-1 drives: a pause is a stop
.fail:
    stc
.out:
    ret

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
    mov byte [di+2], 0x40       ; SubQ
    mov byte [di+3], 1          ; format 1: the current position
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
    call cd_be32                ; pregap, which shows as 0
    or dx, dx
    jns .rel
    xor ax, ax
    xor dx, dx
.rel:
    call cd_secs
    mov cx, ax
    mov si, cd_abuf + 8         ; the absolute address
    call cd_be32
    call cd_secs
    mov dx, ax
    mov bl, [cd_abuf+1]         ; the audio status, 11h..15h
    xor al, al                  ; CDS_IDLE for 15h and anything unknown
    cmp bl, 0x11
    jb .st
    cmp bl, 0x14
    ja .st
    mov al, bl
    sub al, 0x10                ; 11h playing .. 14h error = 1 .. 4
.st:
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
    jz .get
    mov si, [cd_vpage]
    mov [si+9], cl              ; port 0's volume...
    mov [si+11], ch             ; ...and port 1's
    and byte [si], 0x3F         ; PS is not ours to send back
    mov word [cd_abuf], 0       ; the mode data length is reserved in SELECT
    call cd_pclear
    mov byte [di], 0x55         ; MODE SELECT (10), PF
    mov byte [di+1], 0x10
    mov cx, [cd_vpage]          ; what goes back: the header, the descriptor
    sub cx, cd_abuf - 16        ; if any, and the page - as it came
    mov [di+8], cl
    mov si, cd_abuf
    call cd_xin
    mov byte [cd_xout], 1
    call cd_cmd
    jc .out
    call cd_vsense              ; ...and what it holds now
    jc .out
.get:
    mov si, [cd_vpage]
    mov cl, [si+9]
    mov ch, [si+11]
    clc
.out:
    pop di
    pop si
    pop bx
    ret

; cd_vsense - MODE SENSE (10), page 0Eh, into cd_abuf; [cd_vpage] = the page
;             (past the header and any block descriptor). clobbers: AX, CX, SI, DI
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
    mov [cd_vpage], ax
    add ax, 12                  ; the page must have arrived
    sub ax, cd_abuf
    cmp ax, [cd_xgot]
    ja .short
    mov si, [cd_vpage]
    mov al, [si]
    and al, 0x3F
    cmp al, 0x0E
    je .out                     ; CF = 0 off the compare
.short:
    mov ax, CDE_NOAUDIO         ; no audio control page: no volume to set
    stc
.out:
    ret
