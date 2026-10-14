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

; Why an Apply refused: cs_cfg_load's AL, RLAPPLY's ra_says index
CSA_OK      equ 0
CSA_NOFILE  equ 1                   ; no REDLINE.CFG in SYSTEM\APPDATA
CSA_BADFILE equ 2                   ; not one we wrote
CSA_BOARD   equ 3                   ; the fingerprint is another machine's
CSA_ROW     equ 4                   ; a row this build cannot write
CSA_CPU     equ 5                   ; an 8088/8086: nothing to tune
CSA_KNOB    equ 6                   ; a knob the row does not have

cs_cfgname: db 'REDLINE.CFG', 0
cs_cfgsig:  db 'RLCFG', 0, 0, 0
cs_cfgip:   db 0                    ; os88data.inc's in-place stamp
cs_cfg:     times CS_CFG_SZ db 0
cs_cfgrow:  db 0                    ; the row the file names, once checked


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

%ifdef CS_LOADER
; -----------------------------------------------------------------------------
; cs_cfg_load - read REDLINE.CFG and check it against THIS machine.
; out: CF = 0, AL = CSA_OK and [cs_cfgrow] = its row; or CF = 1, AL = CSA_*.
; Writes nothing to any chipset. cs_identify must have run.
; -----------------------------------------------------------------------------
cs_cfg_load:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    mov bx, cs_cfg
    mov cx, CS_CFG_SZ
    xor dx, dx
    mov si, cs_cfgname
    call od_read                    ; DX:AX = the file's size
    mov cl, CSA_NOFILE
    jnc .read
    cmp ax, FERR_NOENT
    je .no
    mov cl, CSA_BADFILE             ; there, and too big to be ours
    jmp .no
.read:
    mov cl, CSA_BADFILE
    or dx, dx
    jnz .no
    cmp ax, CS_CFG_HDR + 2
    jb .no
    cmp ax, CS_CFG_SZ
    ja .no
    mov si, cs_cfgsig
    mov di, cs_cfg
    push cx
    mov cx, 8
    cld
    repe cmpsb
    pop cx
    jne .no
    mov bl, [cs_cfg + 11]           ; 1..8 knobs, and exactly that long
    or bl, bl
    jz .no
    cmp bl, CS_CFG_MAXK
    ja .no
    xor bh, bh
    shl bx, 1
    add bx, CS_CFG_HDR
    cmp ax, bx
    jne .no
    mov cl, CSA_BOARD               ; THE FINGERPRINT: every field
    mov ax, [cs_crc]
    cmp ax, [cs_cfg + 12]
    jne .no
    mov si, cs_date
    mov di, cs_cfg + 14
    push cx
    mov cx, 8
    repe cmpsb
    pop cx
    jne .no
    mov al, [cs_model]
    cmp al, [cs_cfg + 22]
    jne .no
    mov cl, CSA_CPU
    cmp byte [cs_tier], CPU_8086
    je .no
    mov cl, CSA_ROW                 ; a row we know, that can write
    mov ax, [cs_cfg + 8]
    mov si, cs_rows
    xor bx, bx
.row:
    cmp bx, CS_NROWS
    jae .no
    cmp ax, [si + CSR_CODE]
    je .found
    add si, CSR_SIZE
    inc bx
    jmp short .row
.found:
    cmp word [si + CSR_APPLY], 0
    je .no
    mov [cs_cfgrow], bl
    mov al, CSA_OK
    clc
    jmp short .out
.no:
    mov al, cl
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; cs_cfg_apply - put the checked setting back: every knob through its row's
; apply proc. out: CF = 0 set; CF = 1 AL = CSA_KNOB (and the knobs before
; the refused one ARE set - a row orders its own knobs, 103.6.2)
cs_cfg_apply:
    push bx
    push cx
    push si
    push di
    mov al, [cs_cfgrow]
    xor ah, ah
    mov bl, CSR_SIZE
    mul bl
    mov bx, ax
    mov di, [cs_rows + bx + CSR_APPLY]
    xor ch, ch
    mov cl, [cs_cfg + 11]
    mov si, cs_cfg + CS_CFG_HDR
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
    pop di
    pop si
    pop cx
    pop bx
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

cs_ap_ne:                           ; C&T NEAT: knobs 1-4 (csne.inc), each
    jmp cs_ne_put                   ; refused outside its field
