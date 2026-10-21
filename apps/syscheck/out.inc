; =============================================================================
; os8088 - apps/syscheck/out.inc - the report and Clean Up (SPEC.md 118.7,
;                                  118.8)
;
; Both run on the UI task from a button, a key or the alert's completion,
; with the gfx lock held - W_ONCLICK's environment, which may call the file
; slots. The report is composed a line at a time in sc_txt with the same
; helpers the window uses, then appended to a transient claim and written in
; one OSAPI_FILE_WRITE. Clean Up removes only findings carrying FL_RM -
; visible debris (118.8) - and rebuilds ASSOC.DAT when a finding asked for it.
; =============================================================================

; -----------------------------------------------------------------------------
; sc_act_report - Save Report: SYSCHECK.TXT in the root of the volume checked
; in:  SI = window; gfx lock held; keeps SI
; -----------------------------------------------------------------------------
sc_act_report:
    mov al, SB_SAVE
    call sc_bok                     ; the key path reaches here ungreyed
    jnc .go
    ret
.go:
    push si
    push es
    mov ax, [sc_nf]                 ; ~100 bytes a finding, and the head
    xor dx, dx
    mov cx, 9
    div cx
    add ax, 3
    call OSAPI_MEM_CLAIM            ; DX = the segment
    jnc .claimed
    mov si, sc_t_nomem
    jmp .toast
.claimed:
    mov [sc_rseg], dx
    mov es, dx
    mov word [sc_rend], 0           ; the report's end, in ES
    ; --- the head ---------------------------------------------------------
    mov si, sc_o_title
    call sc_oline
    mov di, sc_txt
    mov si, sc_o_build
    call sc_scpy
    mov ax, BUILD_NUM
    call dec_u16
    mov si, sc_o_fmt
    call sc_scpy
    mov ax, PKG_FMT
    call dec_u16
    mov si, sc_o_drv
    call sc_scpy
    mov ax, DRV_VER
    call dec_u16
    call sc_otxt
    mov di, sc_txt                  ; "Volume C: hard disk, checked 2026-10-20 17:40"
    mov si, sc_o_vol
    call sc_scpy
    call sc_drive2
    mov si, sc_o_hd
    cmp byte [sc_fixed], 0
    jne .k
    mov si, sc_o_fl
.k:
    call sc_scpy
    mov si, sc_o_checked
    call sc_scpy
    call OSAPI_CLK_GET              ; CX year, DH month, DL day, BH:BL hh:mm
    push bx
    push dx
    mov ax, cx                      ; the year as four digits: dec_u16
    mov cl, 100                     ; would put DEC_SEP's comma in it
    div cl                          ; AL = the century, AH = the rest
    push ax
    call sc_two
    pop ax
    mov al, ah
    call sc_two
    mov byte [di], '-'
    inc di
    pop dx
    push dx
    mov al, dh
    call sc_two
    mov byte [di], '-'
    inc di
    pop dx
    mov al, dl
    call sc_two
    mov byte [di], ' '
    inc di
    pop bx
    push bx
    mov al, bh
    call sc_two
    mov byte [di], ':'
    inc di
    pop bx
    mov al, bl
    call sc_two
    call sc_otxt
    call sc_cnttext
    call sc_otxt
    mov di, sc_txt                  ; "Result: 3 broken, 7 warnings"
    mov si, sc_o_result
    call sc_scpy
    call sc_totals
    mov ax, cx
    or ax, dx
    jnz .some
    mov si, sc_st_clear
    call sc_scpy
    jmp short .res
.some:
    mov ax, cx
    call dec_u16
    mov si, sc_s_broken
    call sc_scpy
    mov si, sc_s_comma
    call sc_scpy
    mov ax, dx
    call dec_u16
    mov si, sc_s_warning
    call sc_scpy
    cmp dx, 1
    je .res
    mov byte [di], 's'
    inc di
    mov byte [di], 0
.res:
    call sc_otxt
    mov si, sc_s_none
    call sc_oline
    ; --- one line a check -------------------------------------------------
    xor ax, ax
.row:
    push ax
    mov bx, ax
    shl bx, 1
    mov si, [sc_rnames+bx]
    mov di, sc_txt
    call sc_scpy
    call sc_padto28
    pop ax
    push ax
    push di
    call sc_rowres                  ; sc_rsn
    pop di
    mov si, sc_rsn
    call sc_scpy
    call sc_otxt
    pop ax
    inc ax
    cmp ax, SC_NCHK
    jb .row
    mov si, sc_s_none
    call sc_oline
    ; --- every kept finding ----------------------------------------------
    mov si, sc_o_findings
    cmp word [sc_nf], 0
    jne .fh
    mov si, sc_o_nofind
.fh:
    call sc_oline
    xor cx, cx
.f:
    cmp cx, [sc_nf]
    jae .lost
    push cx
    mov ax, FR_SIZE
    mul cx
    mov si, ax
    add si, sc_ftab
    push si
    mov al, [si+FR_SEV]             ; BROKEN / WARNING / NOTE
    mov bx, sc_o_note
    cmp al, SV_WARN
    jb .sv
    mov bx, sc_o_warn
    je .sv
    mov bx, sc_o_brk
.sv:
    mov di, sc_txt
    mov si, bx
    call sc_scpy
    pop si
    push si
    lea si, [si+FR_PATH]
    call sc_scpy
    mov byte [di], ' '
    inc di
    mov byte [di], ' '
    inc di
    pop si
    lea si, [si+FR_RSN]
    call sc_scpy
    call sc_otxt
    pop cx
    inc cx
    jmp short .f
.lost:
    mov ax, [sc_flost]
    or ax, ax
    jz .write
    mov di, sc_txt
    mov si, sc_o_more
    call sc_scpy
    call dec_u16
    mov si, sc_o_more2
    call sc_scpy
    call sc_otxt
.write:
    ; --- to the root of the volume checked, and home again --------------
    xor dx, dx
    call sc_goto
    mov cx, [sc_rend]               ; CX = the length
    jc .nowr
    mov si, sc_n_report
    xor bx, bx                      ; ES:BX = the text
    xor dx, dx                      ; DX:CX = its bytes
    call OSAPI_FILE_WRITE
.nowr:
    pushf
    call sc_gohome
    mov dx, [sc_rseg]
    call OSAPI_MEM_FREE
    popf
    mov si, sc_t_saved
    jnc .ok
    mov si, sc_t_notsaved
    jmp short .toast
.ok:
    mov al, [sc_vlet]
    mov [sc_t_saved + 6], al        ; "Saved C:\SYSCHECK.TXT"
.toast:
    push ds
    pop es
    xor cx, cx
    call OSAPI_TOAST
    pop es
    pop si
    ret

; sc_otxt - sc_txt, then CR LF, onto the report at ES:[sc_rend]
; sc_oline - the NUL string at SI, then CR LF, likewise (keeps all but SI)
sc_otxt:
    mov si, sc_txt
sc_oline:
    push ax
    push bx
    mov bx, [sc_rend]
.c:
    lodsb
    or al, al
    jz .e
    mov [es:bx], al
    inc bx
    jmp short .c
.e:
    mov word [es:bx], 0x0A0D        ; CR, LF
    inc bx
    inc bx
    mov [sc_rend], bx
    pop bx
    pop ax
    ret

; sc_padto28 - spaces at DI up to sc_txt + 28 (a report column)
sc_padto28:
    cmp di, sc_txt + 28
    jae .out
    mov byte [di], ' '
    inc di
    jmp short sc_padto28
.out:
    mov byte [di], 0
    ret

; sc_two - AL (0..99) as two digits at DI
sc_two:
    push ax
    aam                             ; AH = tens, AL = units
    add ax, 0x3030
    xchg al, ah
    mov [di], ax
    inc di
    inc di
    mov byte [di], 0
    pop ax
    ret

; sc_gohome - put the instance back where it stood before the scan
sc_gohome:
    push bx
    push dx
    mov dx, [sc_homedx]
    mov bl, [sc_homebl]
    call OSAPI_FILE_GOTO_QM
    pop dx
    pop bx
    ret

; -----------------------------------------------------------------------------
; sc_act_clean - Clean Up...: ask first (SPEC.md 118.8)
; in:  SI = window; gfx lock held; keeps SI
; -----------------------------------------------------------------------------
sc_act_clean:
    push si
    mov di, sc_q                    ; "Remove 4 leftover files?"
    mov ax, [sc_nrm]
    or ax, ax
    jz .assoc
    mov si, sc_q_rm
    call sc_scpy
    call dec_u16
    mov si, sc_q_rm2
    cmp byte [sc_ascbad], 0
    je .q
    mov si, sc_q_rm3                ; ...and the associations as well
.q:
    call sc_scpy
    jmp short .ask
.assoc:
    mov si, sc_q_asc
    call sc_scpy
.ask:
    pop si
    push si
    mov bx, si
    mov al, OS88UI_ADANGER          ; Cancel / Proceed, Cancel the default
    mov si, sc_q
    mov di, sc_cleandone
    call os88ui_ask
    pop si
    ret

; sc_cleandone - the alert's answer: AL = 1 is Proceed
; in:  SI = window; gfx lock held
sc_cleandone:
    cmp al, 1
    je .go
    ret
.go:
    push si
    push bp
    push es
    push ds
    pop es
    xor bp, bp                      ; BP = files removed
    xor cx, cx
.f:
    cmp cx, [sc_nf]
    jae .assoc
    push cx
    mov ax, FR_SIZE
    mul cx
    mov di, ax
    add di, sc_ftab
    test byte [di+FR_FLG], FL_RM
    jz .next
    mov dx, [di+FR_CLUS]
    call sc_goto
    jc .next
    lea si, [di+FR_PATH]            ; the name: after the path's last '\'
    mov bx, si
.ls:
    lodsb
    or al, al
    jz .name
    cmp al, '\'
    jne .ls
    mov bx, si
    jmp short .ls
.name:
    mov si, bx
    call OSAPI_FILE_DELETE
    jc .next
    inc bp
.next:
    pop cx
    inc cx
    jmp short .f
.assoc:
    cmp byte [sc_ascbad], 0
    je .home
    xor dx, dx
    call sc_goto
    mov dl, [sc_vol]
    mov al, ASSOC_FL_FULL           ; read every package afresh (SPEC.md 54)
    call OSAPI_ASSOC_FLUSH
.home:
    call sc_gohome
    mov di, sc_txt                  ; "Removed 4 files"
    mov si, sc_t_removed
    call sc_scpy
    mov ax, bp
    call dec_u16
    mov si, sc_t_files
    call sc_scpy
    mov si, sc_txt
    xor cx, cx
    call OSAPI_TOAST
    pop es
    pop bp
    pop si
    call sc_scan_begin              ; and look again
    call sc_repaint
    jmp sc_kick

; --- the report's words -----------------------------------------------------------
sc_n_report:   db 'SYSCHECK.TXT', 0
sc_o_title:    db 'os8088 System Check report', 0
sc_o_build:    db 'Release build ', 0
sc_o_fmt:      db ', package format ', 0
sc_o_drv:      db ', driver version ', 0
sc_o_vol:      db 'Volume ', 0
sc_o_hd:       db ' hard disk', 0
sc_o_fl:       db ' floppy', 0
sc_o_checked:  db ', checked ', 0
sc_o_result:   db 'Result: ', 0
sc_o_findings: db 'Findings', 0
sc_o_nofind:   db 'No findings', 0
sc_o_brk:      db 'BROKEN   ', 0
sc_o_warn:     db 'WARNING  ', 0
sc_o_note:     db 'NOTE     ', 0
sc_o_more:     db '(', 0
sc_o_more2:    db ' more findings were not kept)', 0
; the toasts - each fits SPEC.md 59.10's 24 characters
sc_t_saved:    db 'Saved ?:\SYSCHECK.TXT', 0
sc_t_notsaved: db 'Report not saved', 0
sc_t_nomem:    db 'Not enough memory', 0
sc_t_removed:  db 'Removed ', 0
sc_t_files:    db ' files', 0
; the question - at most OS88UI_AMAX (34) characters
sc_q_rm:       db 'Remove ', 0
sc_q_rm2:      db ' leftover files?', 0
sc_q_rm3:      db ' files, fix associations?', 0
sc_q_asc:      db 'Rebuild the file associations?', 0
