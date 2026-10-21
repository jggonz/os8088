; =============================================================================
; os8088 - HDDTOOL.DRV / ipkg.inc
;
; HDT_PKG: the installer, for a PACKAGE (SPEC.md 117.4). INSTALL.O88's wizard
; reaches this through HDD.DRV's door (HDV_TOOL), which forwards it here as
; one verb with AH = an HDI_* and ES = the package's segment.
;
; **NOTHING HERE INSTALLS ANYTHING.** Every verb sets the Install window's own
; state - [hd_idev], [hd_isel], [hd_iwipe], [hd_iw_st] - and calls the Install
; window's own routines: hd_iw_scan for what the slots are, hd_iact_ok for
; whether one may be taken, hd_inst_step for the install. Their drawing goes
; through hd_iorg, which draws nothing when that window is not on screen, so
; the engine was already headless and this file is a second caller of it and
; not a second copy (SPEC.md 117).
;
; ONE RUN AT A TIME. While the Install window is VISIBLE it owns that state,
; so the verbs that would move it refuse; and a run of OURS that has reached
; the apps-disk prompt holds ASSOC.DAT's writer (SPEC.md 54.7.6.4), so
; hd_tbusy answers busy for it ([hd_ipkg]) - a Control Panel close cannot free
; this image under a run that is half done.
;
; Every verb: in DS = ours, ES = the package's; out CF = 0 and AL = the
; answer; every other register preserved.
; =============================================================================

%if HDG_FAIL != HII_IDLE || HDG_APPS != HII_SYSDONE || HDG_DONE != HII_DONE
  %error "HDG_* (apps/os88api.inc) is HII_*'s numbering, and has drifted"
%endif
%if HDS_OK != HIS_OK || HDS_SMALL != HIS_SMALL || HDS_ALIEN != HIS_ALIEN || HDS_NONE != HIS_NONE || HDS_LIVE != HIS_LIVE
  %error "HDS_* (apps/os88api.inc) is HIS_*'s numbering, and has drifted"
%endif
%if HDIS_TEXTSZ < 25
  %error "HDIS_TEXT must hold the State column's longest row (24) and its NUL"
%endif

hd_ipkg:     db 0               ; 1 = the run at HII_SYSDONE is a PACKAGE's

; -----------------------------------------------------------------------------
; hd_pkgv - HDT_PKG's dispatcher
; -----------------------------------------------------------------------------
hd_pkgv:
    cld                         ; the copies below are lodsb/stosb
    cmp ah, HDI_DEVS
    je hd_pv_devs
    cmp ah, HDI_SLOTS
    je hd_pv_slots
    cmp ah, HDI_GO
    je hd_pv_go
    cmp ah, HDI_APPS
    je hd_pv_apps
    cmp ah, HDI_QUIT
    je hd_pv_quit
    stc                         ; a verb from a newer package than this tool
    ret

; hd_pv_win - CF = 0 = the Install window is ON SCREEN, and owns the state.
; Preserves every register (hd_iorg answers in DI/BP)
hd_pv_win:
    push di
    push bp
    call hd_iorg
    pop bp
    pop di
    ret

; -----------------------------------------------------------------------------
; hd_pv_devs - HDI_DEVS: ES:DI = 4 HDIV_* records. out AL = how many
; -----------------------------------------------------------------------------
hd_pv_devs:
    push bx
    push dx
    push si
    push di
    call hd_sync                ; the resident's table, as the page sees it
    xor bx, bx                  ; BL = the device
.dev:
    cmp bl, [hd_ndev]
    jae .done
    mov al, bl
    push di
    call hd_dev_row             ; DI = the row ...
    mov si, di                  ; ... into SI, so DI stays the caller's
    call hd_geom_ok             ; CF = 0 usable - the page's own predicate
    pop di
    cmc
    sbb al, al
    neg al                      ; 1 = usable
    mov [es:di+HDIV_GEOM], al
    mov al, [si+HDD_KIND]
    mov [es:di+HDIV_KIND], al
    mov al, [si+HDD_BIOS]       ; the int 13h drive where the ROM reads it
    or al, al                   ; (a BIOS row, and an IDE row the BIOS also
    jnz .unit                   ; knows, 52.1.1), else the IDE unit
    mov al, [si+HDD_UNIT]
.unit:
    mov [es:di+HDIV_UNIT], al
    mov byte [es:di+3], 0
    mov ax, [si+HDD_HEADS]
    mul word [si+HDD_SPT]       ; <= 255 x 63: AX alone
    mul word [si+HDD_CYL]       ; DX:AX = the drive
    mov [es:di+HDIV_SECS], ax
    mov [es:di+HDIV_SECS+2], dx
    add di, HDIV_SIZE
    inc bx
    jmp short .dev
.done:
    mov al, bl
    pop di
    pop si
    pop dx
    pop bx
    clc
    ret

; -----------------------------------------------------------------------------
; hd_pv_slots - HDI_SLOTS: DL = a device; ES:DI = 4 HDIS_* records.
; out AL = the slot the Install window would select, 0FFh none would do,
;      0FEh no such device / no geometry, 0FDh the Install window is open
; -----------------------------------------------------------------------------
hd_pv_slots:
    push bx
    push cx
    push si
    push di
    mov al, 0xFD
    call hd_pv_win
    jnc .out                    ; the window owns hd_idev and the scan
    call hd_pv_dev              ; CF = 1 = AL = 0FEh
    jc .out
    mov [hd_idev], dl           ; ...exactly as hd_iw_open binds them
    mov byte [hd_isel], 0
    call hd_iw_scan
    xor cx, cx                  ; CL = the slot
.slot:
    mov bx, cx
    mov al, [bx+hd_istate]
    mov [es:di+HDIS_STATE], al
    mov al, [bx+hd_ivol]
    mov [es:di+HDIS_VOL], al
    mov al, [bx+hd_ilet]
    mov [es:di+HDIS_LET], al
    mov al, [bx+hd_itype]
    mov [es:di+HDIS_TYPE], al
    shl bx, 1
    shl bx, 1
    mov ax, [bx+hd_isecs]
    mov [es:di+HDIS_SECS], ax
    mov ax, [bx+hd_isecs+2]
    mov [es:di+HDIS_SECS+2], ax
    mov [hd_irow], cl
    call hd_iw_rowtext          ; the Install window's row, into hd_line...
    push cx
    push di
    mov si, hd_line + HIW_STATEC    ; ...and its State column is ours
    add di, HDIS_TEXT
    mov cx, HDIS_TEXTSZ - 1
.cp:
    lodsb
    or al, al
    jz .end
    stosb
    loop .cp
.end:
    mov byte [es:di], 0
    pop di
    pop cx
    add di, HDIS_SIZE
    inc cx
    cmp cl, 4
    jb .slot
    mov bl, [hd_isel]           ; the scan's own pick: the first usable slot
    xor bh, bh
    mov al, bl
    cmp byte [bx+hd_istate], HIS_OK
    je .out
    mov al, 0xFF
.out:
    pop di
    pop si
    pop cx
    pop bx
    clc
    ret

; hd_pv_dev - is DL a device with a usable geometry? CF = 1 and AL = 0FEh if
; not. Brings the device table up to date first (hd_sync's reason: the page
; underneath can retype a geometry between two of our calls)
hd_pv_dev:
    push di
    call hd_sync
    mov al, 0xFE
    cmp dl, [hd_ndev]
    cmc
    jc .out                     ; past the table
    mov al, dl
    call hd_dev_row
    call hd_geom_ok
    mov al, 0xFE
.out:
    pop di
    ret

; -----------------------------------------------------------------------------
; hd_pv_go - HDI_GO: DL = device, DH = slot, AL = 1 erase / 0 keep;
;            ES:DI = HDI_CAPSZ bytes for the caption
; out AL = an HDG_* - the stage hd_inst_step left - and the caption at ES:DI
; -----------------------------------------------------------------------------
hd_pv_go:
    push bx                     ; hd_pv_out's frame: the engine runs below
    push cx                     ; and promises no register back, and these
    push dx                     ; verbs keep every one but AX (117.4)
    push si
    push di
    push bp
    push es
    mov cl, al                  ; CL = the erase choice
    call hd_pv_win
    jnc hd_pv_busy              ; the Install window owns the state
    mov word [hd_imsg], hd_s_ibad
    cmp dh, 4
    jae .fail
    call hd_pv_dev
    jc .fail
    call hd_ia_rel              ; an earlier run abandoned at its apps prompt
    mov byte [hd_ipkg], 0
    mov [hd_idev], dl           ; the Install window's open, with the slot
    mov [hd_isel], dh           ; the CALLER's: a fresh table, as 52.10.4.3
    call hd_iw_scan             ; requires - and the scan may move the pick,
    mov [hd_isel], dh           ; so it is put back after it
    mov bl, dh
    xor bh, bh
    neg cl                      ; non-zero -> CF...
    sbb cl, cl
    neg cl                      ; ...CL = 0 keep / 1 erase
    cmp byte [bx+hd_ivol], 0
    jne .wipe
    mov cl, 1                   ; nothing to keep: the Erase box is SET AND
.wipe:                          ; GREYED there (52.10.15), so erase it is
    mov [hd_iwipe], cl
    mov byte [hd_iw_st], HII_IDLE
    call hd_iact_ok             ; the Install button's ONE predicate (47 rule
    jc .fail                    ; 2): what it greys, this refuses
    mov byte [hd_iautod], 0xFF  ; a fresh run asks the machine again
    mov byte [hd_iw_st], HII_ARMED  ; the wizard's Confirm page WAS the confirm
    call hd_inst_step
    jmp short hd_pv_stage
.fail:
    mov byte [hd_iw_st], HII_IDLE
    mov byte [hd_ipkg], 0
    jmp short hd_pv_stage

; -----------------------------------------------------------------------------
; hd_pv_apps - HDI_APPS: the apps phase of the run HDI_GO left waiting.
;              ES:DI = HDI_CAPSZ bytes. out as HDI_GO
; -----------------------------------------------------------------------------
hd_pv_apps:
    push bx                     ; hd_pv_out's frame: the engine runs below
    push cx                     ; and promises no register back, and these
    push dx                     ; verbs keep every one but AX (117.4)
    push si
    push di
    push bp
    push es
    call hd_pv_win
    jnc hd_pv_busy
    mov word [hd_imsg], hd_s_pvnone
    cmp byte [hd_iw_st], HII_SYSDONE
    jne .fail                   ; nothing waiting: the tool was reloaded
    cmp byte [hd_ipkg], 0       ; ...or the run is the Install window's
    je .fail
    call hd_inst_step
    jmp short hd_pv_stage
.fail:
    mov byte [hd_iw_st], HII_IDLE
    mov byte [hd_ipkg], 0
                                ; ...and on into hd_pv_stage

; hd_pv_stage - the two install verbs' common tail: AL = the stage the engine
; is in, [hd_ipkg] set when that stage is a run of OURS waiting on its apps
; disk, [hd_imsg] to ES:DI. Pops what both verbs pushed.
hd_pv_stage:
    mov al, [hd_iw_st]
    cmp al, HII_SYSDONE
    jne .cap
    mov byte [hd_ipkg], 1
.cap:
    mov si, [hd_imsg]
    jmp short hd_pv_out

; hd_pv_busy - the Install window is on screen: refuse and touch NOTHING of
; its state - not the stage, not [hd_imsg], which is its caption
hd_pv_busy:
    xor al, al                  ; HDG_FAIL
    mov si, hd_s_pvwin
hd_pv_out:
    mov bp, sp                  ; THE CALLER'S ES:DI, off the verb's frame:
    mov es, [ss:bp]             ; the engine reads files and claims buffers
    mov di, [ss:bp+4]           ; and promises neither register back
    push ax
    mov cx, HDI_CAPSZ - 1
.cp:
    lodsb
    or al, al
    jz .end
    stosb
    loop .cp
.end:
    mov byte [es:di], 0
    pop ax
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    clc
    ret


; -----------------------------------------------------------------------------
; hd_pv_quit - HDI_QUIT: abandon OUR run at its apps prompt. The system part
; has committed and stays (SPEC.md 52.10.10). A run that is the Install
; window's is left alone. out AL = 0
; -----------------------------------------------------------------------------
hd_pv_quit:
    cmp byte [hd_ipkg], 0
    je .out
    call hd_pv_win
    jnc .out                    ; the window took the run over: it is its own
    call hd_ia_rel
    mov byte [hd_iw_st], HII_IDLE
    mov byte [hd_ipkg], 0
.out:
    xor al, al
    clc
    ret

hd_s_pvwin:  db 'Close the Install window first', 0
hd_s_pvnone: db 'Nothing to continue - install again', 0
