; =============================================================================
; os8088 - apps/waverack/wrsave.inc - Save, Save As, Save Selection As
;          (SPEC.md 123.10)
;
; SAVE NEVER WRITES OVER A FILE UNTIL THE NEW ONE IS WHOLE. It writes
; WRSAVE.$$$ in the destination folder - after OSAPI_FILE_DFREE says the
; volume holds the document's bytes + 44 + a pad byte + one cluster - as the
; canonical 44-byte header (SPEC.md 123.4's writer) and then the frames,
; piece by piece, and only then deletes the target and renames the temp file
; to it. A failure or a cancel before the rename deletes the temp file and
; leaves the original alone; a failure between the delete and the rename
; leaves WRSAVE.$$$ - complete - and says so.
;
; IT IS THE UI TASK'S, A GULP A TICK (SPEC.md 20.6 rule 7: a worker touches
; no file). The frames come through wo_read (the I/O claim, restood in each
; source's folder) into a 16 KB OUTPUT claim, and each tick does ONE disk
; call - a read into it, or a write of it when it is full - so the desktop
; stays live and the status bar says `Saving 45%`. The first write CREATES
; the temp file with OSAPI_FILE_WRITE (16 KB, a whole number of clusters on
; any volume whose cluster fits the claim), and every later one appends with
; OSAPI_FILE_WRITE_SEQ (SPEC.md 18.4.9), its token carried, so an append does
; not walk the chain. Stop or Escape cancels; the transport, the edits and
; the other saves grey while it runs (wr_cmdok: `Still saving`).
;
; SAVING OVER A SOURCE (the file the document was opened from, or one the
; clipboard or the history still names) rebases: once the rename is done
; that file is the NEW bytes, so every piece that named the old ones is
; wrong. The history is cleared, the clipboard's pieces of it dropped, and -
; for a save of the whole document - the document becomes ONE piece of the
; saved file, whose envelope is scanned again (the view, the cursor and the
; selection kept: the frames are the same) and the status bar says `Saved;
; undo cleared`. Save Selection As over a file the document still reads is
; refused (`File is in use`): the document would be left on bytes that are
; gone.
; =============================================================================

WSM_SAVE    equ 0                   ; [ws_mode]: Save - the document's file
WSM_AS      equ 1                   ; Save As - the whole, a new name
WSM_SEL     equ 2                   ; Save Selection As - [s, e)
WS_OUTKB    equ WO_IOKB             ; the output claim's floor, KB: a
                                    ; cluster past it sizes it ([ws_osz])

; -----------------------------------------------------------------------------
; ws_save - File > Save, the SAVE key: to the document's own file; an
;           untitled document (New, then Paste) goes to Save As.
; in:  BX = the window. clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
ws_save:
    cmp byte [wo_titled], 0
    je ws_saveas
    mov byte [ws_mode], WSM_SAVE
    mov si, wo_name
    call ws_tset
    mov ax, [wo_fdir]
    mov [ws_ddir], ax
    mov al, [wo_fvol]
    mov [ws_dvol], al
    jmp ws_go

; ws_saveas / ws_savesel - the Standard File dialog in Save mode (SPEC.md 38),
; the document's name offered; its answer is ws_dlgdone. BX = the window
ws_saveas:
    mov byte [ws_mode], WSM_AS
    jmp short ws_dlg
ws_savesel:
    mov byte [ws_mode], WSM_SEL
ws_dlg:
    mov al, FDLG_SAVE
    mov di, ws_dlgdone
    mov si, wo_name
    call OSAPI_FILE_DLG
    jnc .out
    mov byte [ws_qact], 0
    mov si, wr_s_dlg                ; one is up already
    jmp wr_say
.out:
    ret

; ws_tset - the target's name [ws_tname] from SI (13 bytes, upper case).
; clobbers AX, CX, SI, DI
ws_tset:
    mov di, ws_tname
    mov cx, 13
.c:
    lodsb
    call wr_upper
    mov [di], al
    inc di
    loop .c
    mov byte [di-1], 0
    ret

; -----------------------------------------------------------------------------
; ws_dlgdone - the dialog's answer: SI = the window, ES:DI = the name (the
;              kernel's: copied); the instance stands in the folder chosen,
;              which is the destination. A file already there that is not
;              the document's own is replaced only when the user says so.
;              The dialog lay over the content: repainted whole
; -----------------------------------------------------------------------------
ws_dlgdone:
    push si
    mov si, di
    mov di, wo_nbuf
    mov cx, 13
.c:
    mov al, [es:si]
    call wr_upper
    mov [di], al
    inc si
    inc di
    loop .c
    mov byte [wo_nbuf+13], 0
    mov si, wo_nbuf
    call ws_tset
    call OSAPI_FILE_HERE            ; DX = the folder, BL = the volume
    mov [ws_ddir], dx
    mov [ws_dvol], bl
    call ws_match                   ; a selection over what the document
    jc .rep                         ; reads: refused before any question
    cmp byte [ws_mode], WSM_SEL     ; Save As onto the document's own file:
    je .exist                       ; no question
    call ws_isown
    jnc .go
.exist:
    call wo_find                    ; [wo_nbuf] here? CF = 1 not there
    jc .go
    mov di, wr_qbuf                 ; 'Replace NAME?'
    mov si, wr_s_repl
    call wr_put
    mov si, ws_tname
    call wr_put
    mov word [di], '?'
    pop bx
    push bx
    push es
    mov ax, KERNEL_SEG              ; os88ui_ask reads the caption through ES
    mov es, ax
    mov si, wr_qbuf
    mov di, ws_onrep
    mov al, OS88UI_ADANGER          ; Cancel / Proceed: Enter is Cancel
    call os88ui_ask
    pop es
    jc .noq
    mov byte [ws_rq], 1             ; up: the document's commands grey
    jmp short .rep                  ; until it is answered (review, H2)
.noq:
    mov byte [ws_qact], 0
    jmp short .rep
.go:
    pop bx
    push bx
    call ws_go
.rep:
    pop bx
    jmp wr_repaint

; ws_onrep - 'Replace NAME?' answered: AL = 1 Proceed; SI = the window
ws_onrep:
    mov byte [ws_rq], 0
    mov bx, si
    cmp al, 1
    je .go
.drop:
    mov byte [ws_qact], 0           ; Cancel, or dismissed: nothing written
    ret
.go:
    cmp byte [ws_busy], 0           ; ASKED AGAIN (review, H2): another save
    jne .drop                       ; running is not the one approved
    push bx
    call wt_active                  ; a play under it stops first (L3)
    jc .idle
    call wt_stop
.idle:
    call ws_go
    pop bx
    jmp wr_redraw

; ws_isown - CF = 0 when the target is the document's own file (a titled
; document, the same name, folder and volume). keeps every register
ws_isown:
    cmp byte [wo_titled], 0
    je .no
    push si
    mov si, wo_name
    call ws_issame                  ; ZF = 1 the same
    pop si
    jne .no
    push ax
    mov ax, [wo_fdir]
    cmp ax, [ws_ddir]
    jne .no1
    mov al, [wo_fvol]
    cmp al, [ws_dvol]
    jne .no1
    pop ax
    clc
    ret
.no1:
    pop ax
.no:
    stc
    ret

; ws_issame - ZF = 1 when the NUL name at SI is [ws_tname]. keeps all
ws_issame:
    push ax
    push si
    push di
    mov di, ws_tname
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
; ws_match - [ws_tmask] = a bit a live source record that IS the target (one
;            file can be two records: an Open of what the clipboard already
;            holds), and a Save Selection As over one the document still
;            reads REFUSED (`File is in use`) - asked before the replace
;            question, so the user is not asked to replace what cannot be.
; out: CF = 1 refused (said). clobbers AX, BX, CX, DX, SI
; -----------------------------------------------------------------------------
ws_match:
    xor dx, dx
    xor bx, bx
    mov si, wo_src
.m:
    cmp byte [si+WS_USED], 0
    je .mn
    call ws_issame
    jne .mn
    mov ax, [si+WS_DIR]
    cmp ax, [ws_ddir]
    jne .mn
    mov al, [si+WS_VOL]
    cmp al, [ws_dvol]
    jne .mn
    mov cl, bl
    mov ax, 1
    shl ax, cl
    or dx, ax
    cmp byte [ws_mode], WSM_SEL
    jne .mn
    call wx_docref
    jc .mn
    mov si, wr_s_inuse
    call ws_no
    stc
    ret
.mn:
    add si, WS_SIZE
    inc bx
    cmp bx, WO_NSRC
    jb .m
    mov [ws_tmask], dx
    clc
ws_ret:
    ret

; -----------------------------------------------------------------------------
; ws_go - THE SAVE, BEGUN: the sources that are the target found, a
;         selection over one the document reads refused, the range, the
;         size, the free space, the output claim and the header - then the
;         timer writes it (ws_step). in: BX = the window. clobbers AX, BX,
;         CX, DX, SI, DI
; -----------------------------------------------------------------------------
ws_go:
    call ws_match
    jc ws_ret
    ; --- the range, and its bytes: (f1 - f0) << shift, under 2 GB ----------
    xor ax, ax
    xor dx, dx
    mov bx, [wo_frames]
    mov cx, [wo_frames+2]
    cmp byte [ws_mode], WSM_SEL
    jne .rng
    mov ax, [wo_s]
    mov dx, [wo_s+2]
    mov bx, [wo_e]
    mov cx, [wo_e+2]
.rng:
    mov [ws_f], ax
    mov [ws_f+2], dx
    mov [ws_f0], ax
    mov [ws_f0+2], dx
    mov [ws_f1], bx
    mov [ws_f1+2], cx
    sub bx, ax
    sbb cx, dx
    mov al, [wo_sh]
.sh:
    or al, al
    jz .shd
    shl bx, 1
    rcl cx, 1
    jc .big
    dec al
    jmp short .sh
.shd:
    cmp cx, 0x7FFF                  ; with the header and a pad: under 2^31
    jb .fit
    ja .big
    cmp bx, 0xFFFF - 46
    jbe .fit
.big:
    mov si, ww_e10                  ; 'File over 2 GB'
    jmp ws_no
.fit:
    mov [ws_bytes], bx
    mov [ws_bytes+2], cx
    ; --- the destination: its cluster, its free space -------------------------
    call ws_dest
    jc .wfail
    call OSAPI_FILE_DFREE           ; DX:AX = free, BX = sectors a cluster
    jnc .df
.wfail:
    mov si, wr_s_wfail
    jmp ws_no
.df:
    push ax
    mov ax, bx
    mov cl, 9
    shl ax, cl
    mov [ws_dclu], ax
    pop ax
    mov si, ww_e13                  ; 'Cluster too large': 0 is a 64 KB one,
    cmp word [ws_dclu], 0           ; past FAT16's 32 KB at <= 2 GB
    je ws_no
    cmp word [ws_dclu], WO_MAXCLU
    ja ws_no
    sub ax, [ws_bytes]              ; free - bytes - 45 - a cluster
    sbb dx, [ws_bytes+2]
    jc .full
    sub ax, 45
    sbb dx, 0
    jc .full
    sub ax, [ws_dclu]
    sbb dx, 0
    jnc .room
.full:
    mov si, wr_s_dfull
    jmp ws_no
.room:
    mov ax, [ws_dclu]               ; the I/O claim fit for the destination's
    call wo_iofit                   ; cluster too - a document rebased onto
    mov si, ww_e15                  ; the save reads it ('Not enough memory')
    jc ws_no
    mov ax, [ws_dclu]               ; the output claim: whole clusters, so
    mov cl, 10                      ; every write but the last is (16 KB,
    shr ax, cl                      ; or one 32 KB cluster)
    cmp ax, WS_OUTKB
    jae .okb
    mov ax, WS_OUTKB
.okb:
    push ax
    call OSAPI_MEM_CLAIM
    pop ax
    mov si, ww_e15
    jc ws_no
    mov cl, 10
    shl ax, cl                      ; (32 KB at most: a word)
    mov [ws_osz], ax
    mov [ws_oseg], dx
    call ws_tmpmake                 ; the temp file: a name of its own, made
    jnc .made                       ; empty now (CF: SI = why)
    call ws_end
    jmp ws_no
.made:
    call ws_header
    mov word [ws_fill], 44
    xor al, al
    mov [ws_padded], al
    mov byte [ws_pct], 0FFh
    mov byte [ws_busy], 1
    call ws_pctsay
    jmp wr_arm

; ws_no - the save refused before anything was written: the status bar says
; SI, and a question's deferred action is dropped. clobbers nothing else
ws_no:
    mov byte [ws_qact], 0
    jmp wr_say

; ws_dest - stand in the destination folder. CF = 1 it is gone. keeps all
ws_dest:
    push ax
    push bx
    push dx
    mov dx, [ws_ddir]
    mov bl, [ws_dvol]
    call OSAPI_FILE_GOTO_QM
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ws_header - the canonical 44 bytes at the output claim's start (SPEC.md
;             123.4's writer): RIFF and its size (36 + data + the pad),
;             WAVE, a 16-byte fmt chunk - PCM, the document's channels, rate,
;             bytes a second, block align and bits - then data and its size.
;             clobbers AX, BX, CX, DX, DI
; -----------------------------------------------------------------------------
ws_header:
    push es
    mov es, [ws_oseg]
    xor di, di
    cld
    mov ax, 'RI'
    stosw
    mov ax, 'FF'
    stosw
    mov ax, [ws_bytes]              ; 36 + bytes + (bytes & 1)
    mov dx, [ws_bytes+2]
    mov bx, ax
    and bx, 1
    add ax, bx
    adc dx, 0
    add ax, 36
    adc dx, 0
    stosw
    mov ax, dx
    stosw
    mov ax, 'WA'
    stosw
    mov ax, 'VE'
    stosw
    mov ax, 'fm'
    stosw
    mov ax, 't '
    stosw
    mov ax, 16
    stosw
    xor ax, ax
    stosw
    inc ax                          ; PCM
    stosw
    mov al, [wo_ch]
    xor ah, ah
    stosw
    mov ax, [wo_rate]
    stosw
    xor ax, ax
    stosw
    mov al, [wo_ba]                 ; bytes a second: rate x block align
    xor ah, ah
    mul word [wo_rate]
    stosw
    mov ax, dx
    stosw
    mov al, [wo_ba]
    xor ah, ah
    stosw
    mov al, [wo_bits]
    stosw
    mov ax, 'da'
    stosw
    mov ax, 'ta'
    stosw
    mov ax, [ws_bytes]
    stosw
    mov ax, [ws_bytes+2]
    stosw
    pop es
    ret

; -----------------------------------------------------------------------------
; ws_step - ONE GULP of the save, the timer's: the output claim written when
;           it is full, else as many frames as it has room for read into it;
;           at the end the pad byte, the last write, and the replace.
;           in: BX = the window. clobbers AX, BX, CX, DX, SI, DI, BP, ES
; -----------------------------------------------------------------------------
ws_step:
    mov ax, [ws_fill]
    cmp ax, [ws_osz]
    jb .read
    call ws_flush                   ; full: written (one call)
    jc .out
    jmp short .more
.read:
    mov ax, [ws_f1]                 ; the frames left, at most the room
    mov dx, [ws_f1+2]
    sub ax, [ws_f]
    sbb dx, [ws_f+2]
    mov cx, ax
    or ax, dx
    jz .last
    mov ax, [ws_osz]
    sub ax, [ws_fill]
    push cx
    mov cl, [wo_sh]
    shr ax, cl
    pop cx
    or dx, dx
    jnz .room
    cmp cx, ax
    jbe .n
.room:
    mov cx, ax
.n:
    mov ax, [ws_f]
    mov dx, [ws_f+2]
    mov bp, [ws_osz]
    call wo_read                    ; ES:DI = the frames, CX = how many
    jc .rerr
    jcxz .rerr                      ; nothing, short of the range's end
    add [ws_f], cx
    adc word [ws_f+2], 0
    push cx
    mov cl, [wo_sh]
    pop ax
    shl ax, cl
    mov cx, ax                      ; CX = the bytes
    mov si, di
    mov di, [ws_fill]
    add [ws_fill], cx
    mov ax, es
    mov es, [ws_oseg]
    push ds
    mov ds, ax
    cld
    rep movsb
    pop ds
.more:
    call ws_pctsay
    jmp wr_arm
.rerr:
    mov si, ww_e12                  ; 'Cannot read the file'
    jmp ws_fail
.last:
    cmp byte [ws_padded], 0         ; an odd data length: the pad byte
    jne .flush
    mov byte [ws_padded], 1
    test byte [ws_bytes], 1
    jz .flush
    mov es, [ws_oseg]
    mov di, [ws_fill]
    mov byte [es:di], 0
    inc word [ws_fill]
.flush:
    call ws_flush
    jc .out
    jmp ws_finish
.out:
    ret

; -----------------------------------------------------------------------------
; ws_flush - the output claim's [ws_fill] bytes onto WRSAVE.$$$: the first
;            call creates it, every later one appends (OSAPI_FILE_WRITE_SEQ,
;            the token carried). out CF = 1 failed (ws_fail has said why).
;            clobbers AX, BX, CX, DX, SI, DI, ES
; -----------------------------------------------------------------------------
ws_flush:
    call ws_dest
    jc .io
    mov si, ws_tmp                  ; (made empty by ws_tmpmake: every
    mov es, [ws_oseg]               ; write appends)
    xor bx, bx
    mov cx, [ws_fill]
    jcxz .ok
    mov di, [ws_tok]
    xor al, al                      ; committed every call: a crash keeps
    call OSAPI_FILE_WRITE_SEQ       ; what was written
    jc .err
    mov [ws_tok], di
.ok:
    mov word [ws_fill], 0
    clc
    ret
.io:
    mov ax, FERR_IO
.err:
    call ws_werr                    ; SI = the reason
    call ws_fail
    stc
    ret

; ws_werr - AX = FERR_* -> SI = the status bar's reason. keeps the rest
ws_werr:
    mov si, wr_s_dfull2             ; 'Disk is full'
    cmp ax, FERR_FULL
    je .out
    mov si, wr_s_wprot              ; 'Disk write-protected'
    cmp ax, FERR_WPROT
    je .out
    mov si, wr_s_dirfull            ; 'Folder is full'
    cmp ax, FERR_DIRFULL
    je .out
    mov si, wr_s_wfail              ; 'Cannot write the file'
.out:
    ret

; -----------------------------------------------------------------------------
; ws_fail - THE SAVE STOPS: the temp file deleted (the original never
;           touched), the output claim given back, a question's deferred
;           action dropped, and the status bar says SI. A refinement the
;           save held back goes on. clobbers AX, BX, CX, DX, SI
; -----------------------------------------------------------------------------
ws_fail:
    push si
    cmp byte [ws_made], 0
    je .nt
    mov byte [ws_made], 0
    call ws_dest                    ; (the folder gone: no delete wherever
    jc .nt                          ; the instance stands - review, L7)
    mov si, ws_tmp
    call OSAPI_FILE_DELETE
.nt:
    call ws_end
    mov byte [ws_qact], 0
    pop si
    call wr_say
    ; (falls into ws_rearm)

; ws_rearm - a scan or a refinement the save held back goes on: the timer
; is the scan's too (review, L2: wt_reref re-arms only a refinement)
ws_rearm:
    cmp byte [we_busy], 0
    je .out
    call wr_arm
.out:
    ret

; ws_end - not saving: the output claim back. keeps all but flags
ws_end:
    push dx
    mov dx, [ws_oseg]
    call wo_free
    mov [ws_oseg], dx
    mov byte [ws_busy], 0
    pop dx
    ret

; ws_cancel - Stop or Escape while saving (and the window's close). clobbers
; AX, BX, CX, DX, SI
ws_cancel:
    cmp byte [ws_busy], 0
    je .out
    mov si, wr_s_scancel
    jmp ws_fail
.out:
    ret

; -----------------------------------------------------------------------------
; ws_finish - every byte is in WRSAVE.$$$: the target deleted (absent is
;             fine) and the temp renamed to it; then the sources that were
;             the target rebased, the document named, and a question's
;             deferred action carried out. clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
ws_finish:
    call ws_end
    call ws_dest                    ; the folder gone: the temp is kept where
    mov si, wr_s_wfail              ; it was written, nothing deleted (L7)
    jc .fail
    mov si, ws_tname
    call OSAPI_FILE_DELETE
    jnc .ren
    cmp ax, FERR_NOENT
    je .ren
    mov si, wr_s_norepl             ; it cannot go (read-only): the original
.fail:                              ; stands and the temp file goes
    jmp ws_fail
.ren:
    mov byte [ws_made], 0           ; (the temp is the save from here on)
    mov byte [wo_cv], 0             ; a file under a name may be new bytes
    mov si, ws_tmp                  ; (review, H1: the failure path left the
    mov di, ws_tname                ; window stale)
    call OSAPI_FILE_RENAME
    jnc .named
    mov byte [ws_qact], 0           ; between the delete and the rename:
    mov di, ws_kmsg                 ; the temp is the save, and is KEPT -
    mov si, ws_s_kept               ; 'Kept as WRSAV07.$$$' - and a document
    call wr_put                     ; that read the deleted file reads it
    mov si, ws_tmp                  ; instead
    call wr_put
    mov byte [di], 0
    mov si, ws_tmp
    call ws_rebase
    cmp byte [ws_rb], 1             ; reads it instead (its rescan says so)
    jne .kept
    mov byte [ww_note], N_KEPT      ; ...and is NAMED after it: never titled
    mov si, ws_tmp                  ; with a file that is gone (review, H1)
    jmp short .name
.kept:
    mov si, wr_s_kept
    call wr_say
    jmp ws_rearm
.named:
    mov si, ws_tname
    call ws_rebase
    cmp byte [ws_rb], 2             ; its envelope had no room: the document
    je .out                         ; went, and the status says so (L6)
    cmp byte [ws_mode], WSM_SEL
    je .said
    mov si, ws_tname                ; the whole document: its file now
.name:
    mov di, wo_name
    mov cx, 13
    push es
    push ds
    pop es
    cld
    rep movsb
    pop es
    mov ax, [ws_ddir]
    mov [wo_fdir], ax
    mov al, [ws_dvol]
    mov [wo_fvol], al
    mov byte [wo_titled], 1
    mov byte [wo_dirty], 0
    call wo_gen1                    ; the LCD's name
    call wr_retitle
.said:
    cmp byte [ws_rb], 0             ; rebased: the scan says so when it ends
    jne .act
    mov di, wr_tbuf2                ; 'Saved NAME'
    mov si, wr_s_saved
    call wr_put
    mov si, ws_tname
    call wr_put
    mov byte [di], 0
    mov si, wr_tbuf2
    call wr_say
    call ws_rearm
.act:
    mov al, [ws_qact]               ; New, Open or Close, asked before the
    mov byte [ws_qact], 0           ; save and waiting for it
    or al, al
    jz .out
    mov bx, [wr_win]
    jmp wr_qrun
.out:
    ret

; -----------------------------------------------------------------------------
; ws_rebase - the file SI names in the destination now holds the save: every
;             source record that was the target is stale. With any, the
;             history is cleared and the clipboard's pieces of them dropped;
;             a save of the WHOLE document that read one becomes ONE piece of
;             the saved file - its first such record rewritten in place, its
;             envelope scanned again, the view kept. [ws_rb] = 1 when the
;             document was rebased. clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
ws_rebase:
    mov byte [ws_rb], 0
    cmp word [ws_tmask], 0
    je .out
    push si
    call wx_uclear
    mov dx, [ws_tmask]
    xor bx, bx
.drop:
    shr dx, 1
    jnc .dn
    call wx_clipdrop
.dn:
    inc bx
    cmp bx, WO_NSRC
    jb .drop
    pop si
    cmp byte [ws_mode], WSM_SEL
    je .sweep
    mov dx, [ws_tmask]              ; the first record the document reads
    xor bx, bx
.f:
    shr dx, 1
    jnc .fn
    call wx_docref
    jnc .re
.fn:
    inc bx
    cmp bx, WO_NSRC
    jb .f
    jmp .sweep
.re:
    push si
    mov ax, bx
    call wx_srec                    ; BX = the record
    pop si
    mov di, bx
    push ax
    mov dx, [di+WS_ENV]
    call wo_free
    mov [di+WS_ENV], dx
    mov cx, 13                      ; its name, folder and data
    push es
    push ds
    pop es
    cld
    rep movsb
    pop es
    sub di, 13
    call wo_srcnew                  ; a new file under the record: its
    mov al, [ws_dvol]               ; cursor cold (os88rseq.inc), its reads
    mov dx, [ws_dclu]               ; untimed, its read sized for where it
    call wo_rdsize                  ; is now
    mov [di+WS_RDSZ], cx
    mov ax, [ws_ddir]
    mov [di+WS_DIR], ax
    mov al, [ws_dvol]
    mov [di+WS_VOL], al
    mov word [di+WS_DOFF], 44
    mov word [di+WS_DOFF+2], 0
    mov ax, [wo_frames]
    mov [di+WS_FRM], ax
    mov ax, [wo_frames+2]
    mov [di+WS_FRM+2], ax
    mov ax, [ws_dclu]
    mov [di+WS_CLU], ax
    mov ax, [wo_rate]
    mov [di+WS_RATE], ax
    mov al, [wo_bits]
    mov [di+WS_BITS], al
    mov al, [wo_ch]
    mov [di+WS_CH], al
    mov al, [wo_sh]
    mov [di+WS_SH], al
    call we_envsize                 ; CL = the block shift, BX = blocks, AX
    mov [di+WS_BS], cl              ; = KB
    mov [di+WS_NBLK], bx
    pop bx                          ; BX = its index
    or ax, ax
    jz .noenv
    call OSAPI_MEM_CLAIM
    jnc .env
    call wo_new                     ; no room for its envelope: nothing to
    mov byte [ws_rb], 2             ; show it with - and ws_finish must not
    mov si, ww_e15                  ; name and clean the empty document over
    jmp wr_say                      ; this (review, L6)
.env:
    mov [di+WS_ENV], dx
.noenv:
    mov word [wo_npc], 1            ; the document: one piece, the whole file
    mov [wo_pc + WP_SRC], bx
    xor ax, ax
    mov [wo_pc + WP_FIRST], ax
    mov [wo_pc + WP_FIRST+2], ax
    mov ax, [wo_frames]
    mov [wo_pc + WP_CNT], ax
    mov ax, [wo_frames+2]
    mov [wo_pc + WP_CNT+2], ax
    mov [we_ssrc], di
    mov byte [ws_rb], 1
    mov byte [ww_err], 0
    mov byte [ww_note], N_REBASE    ; 'Saved; undo cleared', when it ends
    call wo_sweep
    call wo_moved
    jmp we_rescan
.sweep:
    call wo_sweep
.out:
    ret

; ws_pctsay - 'Saving 45%' when the percentage moved. clobbers AX, BX, CX,
; DX, DI
ws_pctsay:
    mov ax, [ws_f]                  ; (f - f0) * 100 / (f1 - f0), both scaled
    mov dx, [ws_f+2]                ; until the span fits a word
    sub ax, [ws_f0]
    sbb dx, [ws_f0+2]
    mov bx, [ws_f1]
    mov cx, [ws_f1+2]
    sub bx, [ws_f0]
    sbb cx, [ws_f0+2]
.sc:
    jcxz .div
    shr cx, 1
    rcr bx, 1
    shr dx, 1
    rcr ax, 1
    jmp short .sc
.div:
    or bx, bx
    jnz .d
    mov ax, 100                     ; nothing to read: the header alone
    jmp short .p
.d:
    mov cx, bx
    mov bx, 100
    call wo_muldiv
.p:
    cmp al, [ws_pct]
    je .out
    mov [ws_pct], al
    push si
    mov di, wr_tbuf2
    mov si, wr_s_saving
    call wr_put
    xor ah, ah
    call dec_u16
    mov word [di], '%'
    mov si, wr_tbuf2
    call wr_say
    pop si
.out:
    ret

ws_tmpbase: db 'WRSAV00.$$$', 0     ; the temp's shape: two digits at +5
ws_s_kept:  db 'Kept as ', 0
ws_s_notmp: db 'No free temp name', 0

; -----------------------------------------------------------------------------
; ws_tmpmake - THE TEMP FILE (wave 4's review, H1 and M1): WRSAVnn.$$$ in the
;              destination, nn counted on from the window's own number, the
;              first name that is not the target and NOT THERE - so a save
;              never replaces a file it did not make: not another instance's
;              temp, not a kept one a document still reads - made EMPTY at
;              once (OSAPI_FILE_WRITE, DX:CX = 0) so nothing else can take
;              the name between this and the first write; [ws_made] = 1.
; out: CF = 1 none made, SI = why. clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
ws_tmpmake:
    call ws_dest
    mov si, wr_s_wfail
    jc .no
    mov ax, [wr_win]                ; the window's record: its own number
    shr ax, 1
    shr ax, 1
    xor dx, dx
    mov cx, 100
    div cx
    mov [ws_tn], dl
    mov byte [ws_tcnt], 100
.try:
    mov si, ws_tmpbase
    mov di, ws_tmp
    call wr_put
    mov byte [di], 0
    mov al, [ws_tn]
    aam                             ; AH = tens, AL = units
    add ax, '00'
    mov [ws_tmp+5], ah
    mov [ws_tmp+6], al
    mov si, ws_tmp                  ; not the target...
    call ws_issame
    je .next
    mov si, ws_tmp                  ; ...and not there
    mov di, wo_nbuf
    call wr_put
    mov byte [di], 0
    call wo_find
    jnc .next
    push es
    mov si, ws_tmp
    xor bx, bx
    xor cx, cx
    xor dx, dx
    call OSAPI_FILE_WRITE           ; made, empty: ours
    pop es
    jc .err
    mov byte [ws_made], 1
    mov word [ws_tok], 0
    clc
    ret
.next:
    inc byte [ws_tn]
    cmp byte [ws_tn], 100
    jb .n
    mov byte [ws_tn], 0
.n:
    dec byte [ws_tcnt]
    jnz .try
    mov si, ws_s_notmp
.no:
    stc
    ret
.err:
    call ws_werr
    stc
    ret
