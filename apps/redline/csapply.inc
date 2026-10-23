; =============================================================================
; apps/redline/csapply.inc - REDLINE.CFG, and putting a setting back
; (SPEC.md 103.6.4, 103.6.5)
;
; Shared as source by REDLINE (Save) and RLAPPLY (Apply and nothing else -
; assembled with CS_LOADER, the only build that carries cs_cfg_load and
; cs_cfg_apply. Putting a setting back is RLAPPLY's job, and REDLINE has no
; second way to do it, SPEC.md 103.6.5). Needs csrows.inc (the rows, the fingerprint) and the rows' knob
; files before it, and apps/os88data.inc assembled with OD_BOOTVOL: the file
; lives in SYSTEM\APPDATA on the disk the machine BOOTED from, because that is
; where RLAPPLY is.
;
;   +0   'RLCFG',0,0,0      signature
;   +8   dw  row code       two ASCII (CSR_CODE)
;   +10  db  method         1 data sheet, 2 tuned + one bus clock, 3 tuned
;                           fastest
;   +11  db  n              knobs that follow, 1..CS_CFG_MAXK
;   +12  dw  BIOS CRC-16    the fingerprint at Save
;   +14  8   BIOS date      F000:FFF5
;   +22  db  model byte     F000:FFFE
;   +23  db  0
;   +24  n x {db knob, db value}
;
; THE FINGERPRINT IS THE WHOLE SAFETY OF APPLY. A setting tuned on one board
; never reaches another, or the same board after a BIOS change: every field
; of it must match the machine this is running on, or nothing is written.
; =============================================================================

CS_CFG_MAXK equ 8
CS_CFG_HDR  equ 24
CS_CFG_SZ   equ CS_CFG_HDR + 2 * CS_CFG_MAXK

; Why an Apply refused: cs_cfg_load's AL, which of RLAPPLY's toasts
CSA_OK      equ 0
CSA_NOFILE  equ 1                   ; no REDLINE.CFG in SYSTEM\APPDATA
CSA_BADFILE equ 2                   ; not one we wrote
CSA_BOARD   equ 3                   ; the fingerprint is another machine's
CSA_ROW     equ 4                   ; a row this build cannot write
CSA_CPU     equ 5                   ; an 8088/8086: nothing to tune
CSA_KNOB    equ 6                   ; a knob the row does not have

cs_cfgname: db 'REDLINE.CFG', 0
cs_cfgsig:  db 'RLCFG', 0, 0, 0
%ifdef CS_APPLY_ONLY
; RLAPPLY keeps both of its own in BSS - CS_APPLY_BSS bytes after its image,
; zeroed by the loader, which is all they ever were in the image - and has no
; in-place stamp, writing nothing
cs_cfg      equ os88_image_end
cs_cfgap    equ os88_image_end + CS_CFG_SZ
CS_APPLY_BSS equ CS_CFG_SZ + 2
%else
cs_cfgip:   db 0                    ; os88data.inc's in-place stamp
cs_cfg:     times CS_CFG_SZ db 0
 %ifdef CS_LOADER
cs_cfgap:   dw 0                    ; the checked row's apply proc
 %else
            db 0                    ; REDLINE's: read by nothing (it was the
                                    ; loader's row number, cs_cfgrow)
 %endif
%endif


%ifndef CS_APPLY_ONLY               ; Save is REDLINE's: RLAPPLY never writes
; -----------------------------------------------------------------------------
; cs_cfg_new - start a setting for row AL, method AH, in cs_cfg, stamped with
; this machine's fingerprint (cs_identify has run). Knobs: cs_cfg_knob.
; -----------------------------------------------------------------------------
cs_cfg_new:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    push ds
    pop es
    cld
    mov di, cs_cfg
    mov si, cs_cfgsig
    mov cx, 8
    rep movsb
    push ax
    xor ah, ah
    mov bl, CSR_SIZE
    mul bl
    mov bx, ax
    mov ax, [cs_rows + bx + CSR_CODE]
    stosw                           ; +8 the row code
    pop ax
    mov al, ah
    stosb                           ; +10 the method
    xor al, al
    stosb                           ; +11 no knobs yet
    mov ax, [cs_crc]
    stosw                           ; +12
    mov si, cs_date
    mov cx, 8
    rep movsb                       ; +14
    mov al, [cs_model]
    stosb                           ; +22
    xor al, al
    stosb                           ; +23
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; cs_cfg_knob - AL = knob, AH = value: append it (the excess is dropped)
cs_cfg_knob:
    push bx
    xor bh, bh
    mov bl, [cs_cfg + 11]
    cmp bl, CS_CFG_MAXK
    jae .out
    shl bx, 1
    mov [cs_cfg + CS_CFG_HDR + bx], ax
    inc byte [cs_cfg + 11]
.out:
    pop bx
    ret

; cs_cfg_save - write cs_cfg to the boot disk's SYSTEM\APPDATA
; out: CF = 0 written, else CF = 1 AX = FERR_*
cs_cfg_save:
    push bx
    push cx
    push si
    push di
    push es
    push ds
    pop es
    mov bx, cs_cfg
    xor ch, ch
    mov cl, [cs_cfg + 11]
    shl cx, 1
    add cx, CS_CFG_HDR
    mov si, cs_cfgname
    mov di, cs_cfgip
    call od_write
    pop es
    pop di
    pop si
    pop cx
    pop bx
    ret
%endif                              ; !CS_APPLY_ONLY

%ifdef CS_LOADER
CS_FP_SZ    equ 11                  ; cs_crc, cs_date, cs_model: one run
    times (cs_model - cs_crc) - (CS_FP_SZ - 1) db 0     ; ...asserted: both
    times (CS_FP_SZ - 1) - (cs_model - cs_crc) db 0     ; refuse a gap
; -----------------------------------------------------------------------------
; cs_cfg_load - read REDLINE.CFG and check it against THIS machine.
; out: CF = 0, AL = CSA_OK and [cs_cfgap] = its row's apply proc; or CF = 1,
; AL = CSA_*.
; Writes nothing to any chipset. [cs_tier] and cs_fingerprint must be done
; (cs_identify does both). Clobbers BX, CX, DX, SI, DI and ES (= DS): its one
; caller, RLAPPLY's entry, keeps nothing across it
; -----------------------------------------------------------------------------
cs_cfg_load:
    push ds
    pop es
    mov bx, cs_cfg
    mov cx, CS_CFG_SZ
    xor dx, dx
    mov si, cs_cfgname
    call od_read                    ; DX:AX = the file's size
    jnc .read
    cmp ax, FERR_NOENT
    mov dl, CSA_NOFILE              ; (DL is the answer from here on: CX
    je .no                          ; is the compares')
    mov dl, CSA_BADFILE             ; there, and too big to be ours
    jmp short .no
.read:
    or dx, dx
    mov dl, CSA_BADFILE
    jnz .no
    cmp ax, CS_CFG_HDR + 2
    jb .no
    cmp ax, CS_CFG_SZ
    ja .no
    mov si, cs_cfgsig
    mov di, cs_cfg
    mov cx, 8
    cld
    repe cmpsb
    jne .no
    mov bl, [cs_cfg + 11]           ; n knobs, and exactly that long: a
    xor bh, bh                      ; length of 24 + 2n inside the 26..40
    shl bx, 1                       ; above is n = 1..8 and nothing else
    add bx, CS_CFG_HDR
    cmp ax, bx
    jne .no
    mov dl, CSA_BOARD               ; THE FINGERPRINT: every field. The CRC,
    mov si, cs_crc                  ; the date and the model byte are 11
    mov di, cs_cfg + 12             ; bytes in the same order in csrows.inc
    mov cx, CS_FP_SZ                ; as in the file (+12, +14, +22), so it
    repe cmpsb                      ; is one compare
    jne .no
    mov dl, CSA_CPU
    cmp byte [cs_tier], CPU_8086
    je .no
    mov dl, CSA_ROW                 ; a row we know, that can write
    mov ax, [cs_cfg + 8]
    mov si, cs_rows
.row:
    cmp si, cs_rows_end
    jae .no
    cmp ax, [si + CSR_CODE]
    je .found
    add si, CSR_SIZE
    jmp short .row
.found:
    mov ax, [si + CSR_APPLY]
    or ax, ax
    jz .no
    mov [cs_cfgap], ax
    mov al, CSA_OK
    clc
    jmp short .out
.no:
    mov al, dl
    stc
.out:
    ret

; cs_cfg_apply - put the checked setting back: every knob through its row's
; apply proc. out: CF = 0 set; CF = 1 AL = CSA_KNOB (and the knobs before
; the refused one ARE set - a row orders its own knobs, 103.6.2). Clobbers
; BX, CX, SI and DI, as cs_cfg_load does
cs_cfg_apply:
    mov di, [cs_cfgap]
    xor ch, ch
    mov cl, [cs_cfg + 11]
    mov si, cs_cfg + CS_CFG_HDR
    cmp di, cs_ap_w7                ; the WD76C10: locked as the BIOS left it?
    jne .k                          ; Once, BEFORE the first knob - each access
    pushf                           ; closes behind it, so a sample after one
    cli                             ; reads locked - and cs_id_w7's way: FC72h
    push dx                         ; bit 15 toggles while unlocked (103.6.2)
    mov al, 0x0D
    out 0x70, al
    mov dx, 0xFC72
    in ax, dx
    mov bx, ax
    in ax, dx
    xor ax, bx
    and ah, 0x80
    mov [cs_wd_ul], ah
    pop dx
    popf
.k:
    mov ax, [si]
    call di
    jc .bad
    add si, 2
    loop .k
    mov al, CSA_OK
    clc
    jmp short .out
.bad:
    mov al, CSA_KNOB
    stc
.out:
    ret

%endif                              ; CS_LOADER

; -----------------------------------------------------------------------------
; The rows' apply procs: AL = knob, AH = value. CF = 1 not a knob of the row
; (or a value it refuses); nothing written then.
; -----------------------------------------------------------------------------
cs_ap_fe:                           ; FE3600: knob 4 = R4, 4..15
    cmp al, CS_FE_R4
    jne .no
    cmp ah, CS_FE_FLOOR
    jb .no
    cmp ah, 15
    ja .no
    push ax
    mov al, ah
    call cs_fe_set
    pop ax
    clc
    ret
.no:
    stc
    ret

cs_ap_ne:                           ; the readable rows (csknob.inc): every
    push si                         ; knob through its table, each refused
    mov si, cs_kt_ne                ; outside its field
    jmp short cs_ap_k
cs_ap_ca:
    push si
    mov si, cs_kt_ca
    jmp short cs_ap_k
cs_ap_sm:
    push si
    mov si, cs_kt_sm
    jmp short cs_ap_k
cs_ap_w7:
    push si
    mov si, cs_kt_w7
    jmp short cs_ap_k
cs_ap_o3:
    push si
    mov si, cs_kt_o3
cs_ap_k:
    call cs_kput
    pop si
    ret
