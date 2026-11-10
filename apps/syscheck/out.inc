; =============================================================================
; os8088 - apps/syscheck/out.inc - the report and Clean Up (SPEC.md 118.7,
;                                  118.8)
;
; Both run on the UI task from a button, a key or the alert's completion,
; with the gfx lock held - W_ONCLICK's environment, which may call the file
; slots. The report is composed a line at a time in sc_txt with the same
; helpers the window uses, then appended to a transient claim and written in
; one OSAPI_FILE_WRITE. Clean Up removes only findings carrying FL_RM -
; debris (118.8), a hidden + system one (FL_SYS) through FSCHECK.DRV
; (118.8.1) - and rebuilds ASSOC.DAT when a finding asked for it.
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
    MEMKB_CLAIM                     ; DX = the segment
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
    mov di, sc_txt                  ; "Task: Check the disk" (SPEC.md 118.14)
    mov si, sc_o_task
    call sc_scpy
    mov bl, [sc_task]
    xor bh, bh
    shl bx, 1
    mov si, [sc_tnames+bx]
    call sc_scpy
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
    call sc_rname
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
;
; TWO QUESTIONS, and the second is its own click (118.8.2): the leftovers -
; debris, retired programs and drivers, a stale ASSOC.DAT - and then, when
; the scan found any, the drivers and modules THIS KERNEL REFUSES. Those are
; not debris: each is a file a release once needed, and an install may bring
; it back up to date - but not every install can (the 360KB system disk no
; longer carries XMEM.DRV, SPEC.md 24.3.1.6), and a refused driver does
; nothing but take room. With no leftovers the first question is skipped.
; -----------------------------------------------------------------------------
sc_act_clean:
    mov word [sc_rmd], 0
    mov ax, [sc_nrm]
    or ax, ax
    jnz .q1
    cmp byte [sc_ascbad], 0
    jne .q1
    jmp sc_clean2                   ; only refused drivers: the second alone
.q1:
    push si
    mov di, sc_q                    ; "Remove 4 leftover files?"
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
    mov di, sc_cleandone
    jmp sc_askq

; sc_askq - Cancel / Proceed on sc_q, answered to DI; SI = window, kept
sc_askq:
    push si
    mov bx, si
    mov al, OS88UI_ADANGER          ; Cancel / Proceed, Cancel the default
    mov si, sc_q
    call os88ui_ask
    pop si
    ret

; sc_cleandone - the first alert's answer: AL = 1 is Proceed
; in:  SI = window; gfx lock held
sc_cleandone:
    cmp al, 1
    je .go
    ret                             ; Cancel: nothing, and no second question
.go:
    push es
    push ds
    pop es
    mov bl, FL_RM
    call sc_rmall
    cmp byte [sc_ascbad], 0
    je .done
    xor dx, dx
    call sc_goto
    mov dl, [sc_vol]
    mov al, ASSOC_FL_FULL           ; read every package afresh (SPEC.md 54)
    call OSAPI_ASSOC_FLUSH
.done:
    pop es
    ; fall through: the second question, if there is one

; sc_clean2 - ask about the drivers this kernel refuses (118.8.2), or finish
sc_clean2:
    mov ax, [sc_nstale]
    or ax, ax
    jz sc_cleanfin
    mov di, sc_q                    ; "Also remove 2 unusable files?"
    push si
    mov si, sc_q_st
    cmp word [sc_rmd], 0
    jne .also
    cmp byte [sc_ascbad], 0
    jne .also
    mov si, sc_q_st0                ; asked first: no "Also"
.also:
    call sc_scpy
    call dec_u16
    mov si, sc_q_st2
    call sc_scpy
    pop si
    mov di, sc_staledone
    call sc_askq
    jc sc_cleanfin                  ; refused (one is up): finish here
    ret

; sc_staledone - the second alert's answer: AL = 1 is Proceed
sc_staledone:
    cmp al, 1
    jne sc_cleanfin
    push es
    push ds
    pop es
    mov ax, [sc_rmd]
    push ax
    mov bl, FL_STALE
    call sc_rmall
    pop ax
    cmp ax, [sc_rmd]                ; something went, and a program may have:
    je .same                        ; the associations follow (SPEC.md 54)
    xor dx, dx
    call sc_goto
    mov dl, [sc_vol]
    xor al, al                      ; incremental: rows whose package is gone
    call OSAPI_ASSOC_FLUSH
.same:
    pop es
    ; fall through

; sc_cleanfin - the toast, home, and look again
sc_cleanfin:
    cmp word [sc_rmd], 0            ; nothing done (both answers Cancel
    jne .toast                      ; past the first): no toast, no rescan
    cmp byte [sc_ascbad], 0
    je .out
.toast:
    push si
    call sc_gohome
    mov di, sc_txt                  ; "Removed 4 files"
    mov si, sc_t_removed
    call sc_scpy
    mov ax, [sc_rmd]
    call dec_u16
    mov si, sc_t_files
    call sc_scpy
    mov si, sc_txt
    xor cx, cx
    call OSAPI_TOAST
    pop si
    call sc_scan_begin              ; and look again
    call sc_repaint
    jmp sc_kick
.out:
    ret

; -----------------------------------------------------------------------------
; sc_rmall - remove every kept finding carrying BL's flag; [sc_rmd] counts
; in:  BL = FL_RM or FL_STALE; ES = DS; out: nothing; keeps SI, BP
; -----------------------------------------------------------------------------
sc_rmall:
    push si
    push bp
    xor cx, cx
.f:
    cmp cx, [sc_nf]
    jae .out
    push cx
    push bx
    mov ax, FR_SIZE
    mul cx
    mov di, ax
    add di, sc_ftab
    test [di+FR_FLG], bl
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
    call sc_rmone
    jc .next
    inc word [sc_rmd]
.next:
    pop bx
    pop cx
    inc cx
    jmp short .f
.out:
    pop bp
    pop si
    ret

; sc_rmone - delete the file SI names, in the folder we stand in, the way
; the finding at DI says: an ordinary delete, or FSCHECK.DRV's (118.8.1,
; 118.8.2). out: CF = 1 not removed; keeps DI, SI
sc_rmone:
    test byte [di+FR_FLG], FL_SYS
    jnz .sys
    call OSAPI_FILE_DELETE          ; (a far cell: call it, never jmp)
    ret
.sys:
    push si
    push bx
    push cx
    push dx
    push di
    push es
    mov bx, si                      ; the name, across sc_fload
    call sc_fload                   ; the engine, read beside us once; it
    jc .out                         ; stood us there, so back to the finding
    mov dx, [di+FR_CLUS]
    call sc_goto
    jc .out
    mov si, bx
    mov ah, FSKD_RETIRED
    test byte [di+FR_FLG], FL_STALE
    jnz .refused
    call .call
    jmp short .out
.refused:
    call sc_probe                   ; DX = the KB it expands to
    cmp al, 1
    jne .no
    or dx, dx
    jz .no
    mov cx, dx
    mov ax, dx
    MEMKB_CLAIM                     ; DX = a buffer the engine reads it into
    jc .out
    mov di, dx
    mov ah, FSKD_REFUSED
    call .call
    pushf
    push dx
    mov dx, di
    call OSAPI_MEM_FREE
    pop dx
    popf
    jmp short .out
.no:
    stc
    jmp short .out
.call:
    mov dx, ds                      ; DX:SI = the name
    mov es, [sc_fsel]
    mov al, OSAPI_FSK_DELSYS
    call OSAPI_VOL_CHECK
    ret
.out:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop si
    ret

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
sc_o_task:     db 'Task: ', 0
sc_o_findings: db 'Findings', 0
sc_o_nofind:   db 'No findings', 0
sc_o_brk:      db 'BROKEN   ', 0
sc_o_warn:     db 'WARNING  ', 0
sc_o_note:     db 'NOTE     ', 0
sc_o_more:     db '(', 0
sc_o_more2:    db ' more findings were not kept)', 0
; the toasts - each fits SPEC.md 59.10's 24 characters
TOASTSTR sc_t_saved, 'Saved ?:\SYSCHECK.TXT'
TOASTSTR sc_t_notsaved, 'Report not saved'
TOASTSTR sc_t_nomem, 'Not enough memory'
sc_t_removed:  db 'Removed ', 0
sc_t_files:    db ' files', 0
; the question - at most OS88UI_AMAX (34) characters
sc_q_rm:       db 'Remove ', 0
sc_q_rm2:      db ' leftover files?', 0
sc_q_rm3:      db ' files, fix associations?', 0
sc_q_asc:      db 'Rebuild the file associations?', 0
sc_q_st:       db 'Also remove ', 0
sc_q_st0:      db 'Remove ', 0
sc_q_st2:      db ' unusable files?', 0
