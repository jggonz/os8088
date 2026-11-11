; =============================================================================
; Browser 1.6b: one streamed HTTP download and its native progress window.
; The worker owns sockets; the UI task owns files and the staging claim.
; =============================================================================

%include "brdownload_const.inc"

br_dl_s_open:       db 'Open Link', 0
br_dl_s_busy:       db 'Download Manager is busy', 0
br_dl_s_cancelling: db 'Previous download is cancelling', 0
br_dl_s_nodisk:     db 'No writable destination', 0
br_dl_s_nomem:      db 'Not enough memory for disk cluster', 0
br_dl_s_nodriver:   db 'Network driver unavailable', 0
br_dl_s_nolink:     db 'Network link unavailable', 0
br_dl_s_connect:    db 'Connecting...', 0
br_dl_s_receiving:  db 'Downloading...', 0
br_dl_s_canceling:  db 'Cancelling...', 0
br_dl_s_cancelled:  db 'Cancelled; partial deleted', 0
br_dl_s_delerror:   db 'Could not delete partial file', 0
br_dl_s_kept:       db 'Saved as .$$$; could not rename', 0
br_dl_s_complete:   db 'Download complete.', 0
br_dl_s_http:       db 'HTTP request failed', 0
br_dl_s_headers:    db 'Invalid HTTP headers', 0
br_dl_s_chunked:    db 'Chunked transfer unsupported', 0
br_dl_s_httpcode:   db 'HTTP server returned an error', 0
br_dl_s_incomplete: db 'Connection ended before file completed', 0
br_dl_s_write:      db 'Disk write failed', 0
br_dl_s_stream:     db 'Streaming writes unavailable', 0
br_dl_s_redirect:   db 'Too many or invalid redirects', 0
br_dl_s_url:        db 'Invalid download URL', 0
br_dl_s_cancel:     db 'Cancel', 0
br_dl_s_exit:       db 'Exit', 0
br_dl_s_no:         db '--', 0
br_dl_s_calc:       db '--', 0
br_dl_s_speedunit:  db ' KB/s', 0
br_dl_s_etaunit:    db ' sec', 0
br_dl_s_file:       db 'File:', 0
br_dl_s_source:     db 'Source:', 0
br_dl_s_dest:       db 'Destination:', 0
br_dl_s_status:     db 'Status:', 0
br_dl_s_bytes:      db 'Bytes:', 0
br_dl_s_speed:      db 'Speed:', 0
br_dl_s_eta:        db 'ETA:', 0
br_dl_s_server:     db 'Server', 0
br_dl_s_pc:         db 'Your PC', 0
br_dl_s_default:    db 'DOWNLOAD.BIN', 0
br_dl_ext_htm:      db 'htm', 0
br_dl_ext_html:     db 'html', 0
br_dl_ext_cgi:      db 'cgi', 0
br_dl_ext_php:      db 'php', 0
br_dl_ext_phtml:    db 'phtml', 0
br_dl_ext_shtml:    db 'shtml', 0
br_dl_ext_asp:      db 'asp', 0
br_dl_ext_aspx:     db 'aspx', 0
br_dl_ext_jsp:      db 'jsp', 0
br_dl_ext_pl:       db 'pl', 0      ; Perl CGI and ColdFusion, the period's
br_dl_ext_cfm:      db 'cfm', 0     ; other two page generators, and XHTML,
br_dl_ext_xhtml:    db 'xhtml', 0   ; which br_ctbyte already renders
br_dl_exttab:       dw br_dl_ext_htm, br_dl_ext_html, br_dl_ext_cgi
                    dw br_dl_ext_php, br_dl_ext_phtml, br_dl_ext_shtml
                    dw br_dl_ext_asp, br_dl_ext_aspx, br_dl_ext_jsp
                    dw br_dl_ext_pl, br_dl_ext_cfm, br_dl_ext_xhtml, 0

br_dl_s_nopart:     db 'Download Manager unavailable', 0
br_dl_s_nocold:     db 'Browser part unavailable', 0

; =============================================================================
; THE DOWNLOAD MANAGER LIVES IN PART 0 (SPEC.md 71.20.5, roadmap S2)
;
; The window, the Save dialog's callback, the disk writes and the transfer
; engine are brdownload_engine.inc, assembled into the lazy part with the
; painter (brdownload_paint.inc). What stays here is what must answer while
; the part is NOT in memory - the strings and tables the part reads through
; DS, the suffix test a link click asks (br_dl_autosave), the routines the
; part calls back (br_dl_rtab) - and one THUNK per entry the kernel or the
; Browser calls: a callback's address is an offset in the primary's code
; segment, so a far call into the part has to start here.
;
; WHO MAY DROP THE PART is decided in one place, br_dl_after, which every
; thunk returns through: no manager window, and no download between the Save
; dialog and its end (DL_PICK up to DL_DONE). The worker enters the part only
; from br_dl_step, only in DL_OPEN..DL_BODY, and the UI never moves one of
; those states to an end without the worker first publishing a hand-over from
; HERE, after its far call has returned - so the part is never dropped under
; the worker. And the part never drops itself: the code after an op_drop
; would run in freed memory.
; =============================================================================

; W_PAINT for the manager window (br_dl_tpl): the window exists, so the part is in
br_dl_paint:
    call far [br_dl_vec+BRDL_PAINT]
    ret

; File > Downloads is also the recovery path after the user closes the
; secondary manager window. It is unbound and intentionally reused.
br_dl_show:
    call br_dl_partload
    jc br_dl_ret
    call far [br_dl_vec+BRDL_OPEN]
    jmp short br_dl_after

; AX = link index, or FFFFh for br_ubuf's already-resolved URL (brdownload_
; engine.inc's br_dlx_link). The part comes in before the Save dialog opens:
; the dialog's answer is br_dl_saved, which is the part's
br_dl_link:
    call br_dl_partload
    jc .refused
    call far [br_dl_vec+BRDL_LINK]
    jmp short br_dl_after
.refused:
    push si
    mov si, br_dl_s_nopart          ; no room for it, or BROWSER.O88 gone
    call br_errst
    pop si
    ret

; Also called by br_onwake, to reap a cancel whose manager was closed - which
; leaves the download mid-flight, so the part is in. Not loaded: nothing to do
br_dl_onwake:
    cmp word [br_dl_vec+BRDL_WAKE+2], 0
    je br_dl_ret
    call far [br_dl_vec+BRDL_WAKE]
    jmp short br_dl_after

; The manager window's callbacks: it exists, so the part is in
br_dl_managerclose:
    call far [br_dl_vec+BRDL_CLOSE]
    jmp short br_dl_after
br_dl_timer:
    call far [br_dl_vec+BRDL_TIMER]
    jmp short br_dl_after
br_dl_onup:
    call far [br_dl_vec+BRDL_UP]
    jmp short br_dl_after
br_dl_ondrag:
    call far [br_dl_vec+BRDL_DRAG]
    jmp short br_dl_after
; ...and the Save dialog's: DL_PICK holds the part until it answers
br_dl_saved:
    call far [br_dl_vec+BRDL_SAVED]
    jmp short br_dl_after

; The Browser's own close. With no part in, no download is live: nothing of
; one is open, so only the page fetch is stopped (br_dlx_mainclose's
; close_now does that first too)
br_dl_mainclose:
    call br_imghalt                 ; the image engine stops, its socket closed
                                    ; (SPEC.md 71.26); what it drew stays in
                                    ; case the close is refused
    cmp word [br_dl_vec+BRDL_MAIN+2], 0
    jne .part
    call br_abort
    call br_icclose                 ; the image cache files (SPEC.md 71.27)
    clc
    ret
.part:
    call far [br_dl_vec+BRDL_MAIN]
    jc br_dl_after                  ; refused: the page stays, and its file
    call br_icclose                 ; (flags kept: CF=0, the close goes on)
    ; (falls into br_dl_after)

; br_dl_after - drop part 0 if nothing needs it now. Every register and the
; flags are kept: a close negotiator's CF is its answer
br_dl_after:
    pushf
    cmp byte [br_dl_depth], 0
    jne .keep                       ; a rare action is still inside the part
    cmp byte [br_ctxopen], 0
    jne .keep                       ; a context menu is up: its click is ours
    cmp word [br_dl_win], 0
    jne .keep                       ; the manager is on the glass
    cmp byte [br_dl_state], DL_PICK
    je .keep                        ; the Save dialog will answer br_dl_saved
    cmp byte [br_dl_state], DL_OPEN
    jb .drop
    cmp byte [br_dl_state], DL_DONE
    jb .keep                        ; a transfer, or its end, is in flight
.drop:
    cmp word [br_dl_vec+2], 0
    je .keep                        ; not in
    call br_dl_partdrop
.keep:
    popf
br_dl_ret:
    ret

; br_mapchk - a left click's hit DX:AX (br_hitofs): when its cell is an image
; box whose image has a map, part 0 answers it (SPEC.md 71.32) and CF=0; else
; CF=1 and every register as it was
br_mapchk:
    cmp byte [br_hiflag], 0
    je .no
    push bx
    mov bx, [br_hiimg]
    cmp bx, BR_IMGTMAX
    jae .nob
    shl bx, 1
    cmp word [br_imap + bx], 0
    pop bx
    je .no
    push ax
    push dx
    call br_mapclick                ; CF=0 answered; CF=1 part 0 refused
    pop dx
    pop ax
    ret
.nob:
    pop bx
.no:
    stc
    ret

; --- THE RARELY USED UI IS PART 0'S TOO (SPEC.md 71.24, roadmap S3) ---------
; One thunk each, under the name the Browser always called: the context menu,
; View and Close Source, a form's submit and a bookmark's commit. br_dl_cold
; loads the part - a disk read the first time it is wanted - and counts the
; call in br_dl_depth, so br_dl_after never drops the part while one of them
; is on the stack: the menu's Save Page As opens a dialog whose callbacks can
; repaint (br_paint), and its Save Link As runs br_dl_link's own thunk.
; The callee's registers and CF are its answer: INC and DEC leave CF alone.
br_ctxshow:
    mov byte [br_dl_sel], BRDL_CTXSHOW
    jmp short br_dl_cold
br_ctxclick:                        ; the popup is up, so the part is in
    mov byte [br_dl_sel], BRDL_CTXCLICK
    jmp short br_dl_cold
br_sourcetoggle:
    mov byte [br_dl_sel], BRDL_SRC
    jmp short br_dl_cold
br_submit:
    mov byte [br_dl_sel], BRDL_SUBMIT
    jmp short br_dl_cold
br_mapclick:
    mov byte [br_dl_sel], BRDL_MAPCLICK
    jmp short br_dl_cold
br_bmcommit:
    mov byte [br_dl_sel], BRDL_BMCOMMIT
br_dl_cold:
    call br_dl_partload
    jc .refused
    push bx
    push ax
    mov bl, [br_dl_sel]
    xor bh, bh
    mov ax, [br_dl_vec+bx]
    mov [br_dl_coldvec], ax
    mov ax, [br_dl_vec+bx+2]
    mov [br_dl_coldvec+2], ax
    pop ax
    pop bx
    inc byte [br_dl_depth]
    call far [br_dl_coldvec]
    dec byte [br_dl_depth]
    jmp br_dl_after
.refused:
    push si
    mov si, br_dl_s_nocold          ; no room for it, or BROWSER.O88 gone
    call br_errst
    pop si
    stc
    ret

; Fetch and bind part 0 on the UI task (it is file I/O, SPEC.md 20.6 rule 7).
; Its entry table's offsets become far vectors on the CODE selector - under
; kern_mega a far call through the data one is a #GP the kernel completes per
; call (KERN-MEGA-MEMORY-PLAN MC7) - and the table is READ through the data
; one, as the icons are (br_dl_partdseg). Every real-mode kernel answers the
; same word for both. br_dl_rtab's segments become ours: the part calls back
; through them. Already in: op_fetch says so, and the words are rewritten as
; they were
br_dl_partload:
    push ax
    push cx
    push dx
    push si
    push di
    push es
    mov al, 0
    call op_fetch
    jc .bad
    mov al, 0                       ; op_fetch may clobber AL
    call op_cseg
    or ax, ax
    jz .bad
    mov dx, ax
    mov al, 0
    call op_seg
    or ax, ax
    jz .bad
    mov [br_dl_partdseg], ax
    mov es, ax
    xor si, si
    mov di, br_dl_vec
    mov cx, BRDL_N
.vec:
    mov ax, [es:si]
    mov [di], ax
    mov [di+2], dx
    inc si
    inc si
    add di, 4
    loop .vec
    mov di, br_dl_rtab
    mov cx, BRDL_RN
.back:
    mov [di+2], cs
    add di, 4
    loop .back
    clc
    jmp short .out
.bad:
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; Only through br_dl_after, which knows nothing can be inside the part
br_dl_partdrop:
    push ax
    push cx
    push di
    push es
    mov al, 0
    call op_drop
    push ds
    pop es
    mov di, br_dl_vec
    mov cx, BRDL_N*2
    xor ax, ax
    cld
    rep stosw
    pop es
    pop di
    pop cx
    pop ax
    ret

; br_dl_step - the worker's pass over a download (br_worker, every tick).
; The part's engine does the protocol; this decides WHETHER it runs, and
; publishes what it hands over (see the block comment above)
br_dl_step:
    push ax
    push bx
    mov al, [br_dl_state]
    ; ONLY A TRANSFER IN FLIGHT HAS A STEP: DL_OPEN up to, not including,
    ; DL_DONE. A settled download - DONE, FAIL, CANCEL, or none - is left
    ; alone, whatever request bytes it ended with: br_dl_cancel_req is not
    ; cleared on the way out, and a cancelled download used to come back as
    ; CANCELREADY every tick (SPEC.md 71.20.3)
    cmp al, DL_OPEN
    jb .out
    cmp al, DL_DONE
    jae .out
    mov bx, [br_dl_win]
    cmp byte [br_dl_pending], 0
    jne .wake                       ; the UI has not taken the cluster yet
    cmp byte [br_dl_cancel_req], 0
    jne .cancel
    cmp al, DL_BODY
    ja .out                         ; ENDWAIT, a cancel's states: the UI's move
    mov byte [br_dl_publish], 0
    call far [br_dl_vec+BRDL_STEP]
    mov al, [br_dl_publish]
    or al, al
    jz .out
.publish:
    cmp al, DL_PENDING
    jne .state
    mov byte [br_dl_pending], 1     ; before the state, as the UI reads them
.state:
    mov [br_dl_state], al
.wake:
    call OSAPI_WM_WAKE
    jmp short .out
.cancel:
    cmp al, DL_CANCELREADY
    je .out
    call br_dl_sockclose
    cmp word [br_dl_fill], 0
    je .ready
    mov byte [br_dl_final], 2       ; the staged cluster goes to disk first
    mov al, DL_PENDING
    jmp short .publish
.ready:
    mov byte [br_dl_state], DL_CANCELREADY
    or bx, bx
    jnz .wake
    ; The manager may have been closed while the worker was reaching this
    ; state. The Browser's UI wake reaps the acknowledged cancellation.
    mov bx, [br_win]
    jmp short .wake
.out:
    pop bx
    pop ax
    ret

; --- the primary routines part 0 calls: a far vector each (the part's
; RCALL), whose segment br_dl_partload makes our CS, onto a shim that makes
; the near call and returns far. A near call from the part would land in the
; part's own segment; tools/brdlsyms.py refuses one
br_dl_rtab:
br_dl_r_errst:      dw br_dl_rs_errst, 0
br_dl_r_split:      dw br_dl_rs_split, 0
br_dl_r_hrefpfx:    dw br_dl_rs_hrefpfx, 0
br_dl_r_resolve:    dw br_dl_rs_resolve, 0
br_dl_r_urlgrow:    dw br_dl_rs_urlgrow, 0  ; the transfer's and Copy's buffers
br_dl_r_uibtninit:  dw br_dl_rs_uibtninit, 0
br_dl_r_uibtn:      dw br_dl_rs_uibtn, 0
br_dl_r_uibtnup:    dw br_dl_rs_uibtnup, 0
br_dl_r_uibtndrag:  dw br_dl_rs_uibtndrag, 0
br_dl_r_pagepin:    dw br_dl_rs_pagepin, 0
br_dl_r_pageunpin:  dw br_dl_rs_pageunpin, 0
br_dl_r_abort:      dw br_dl_rs_abort, 0
br_dl_r_net_find:   dw br_dl_rs_net_find, 0
br_dl_r_copyz:      dw br_dl_rs_copyz, 0
br_dl_r_prefix:     dw br_dl_rs_prefix, 0
br_dl_r_sockclose:  dw br_dl_rs_sockclose, 0
br_dl_r_docmap:     dw br_dl_rs_docmap, 0   ; Copy's walk (brcopyselection.inc)
br_dl_r_avail:      dw br_dl_rs_avail, 0    ; the reserve (br_avail)
br_dl_r_toasturl:   dw br_dl_rs_toasturl, 0 ; a form's submit: its toasts,
br_dl_r_toastpost:  dw br_dl_rs_toastpost, 0 ; here where their text is
; ...and the rarely used UI's (brcold.inc, SPEC.md 71.24)
br_dl_r_measure:       dw br_dl_rs_measure, 0
br_dl_r_hitofs:        dw br_dl_rs_hitofs, 0
br_dl_r_linkat:        dw br_dl_rs_linkat, 0
br_dl_r_paint_band:    dw br_dl_rs_paint_band, 0
br_dl_r_sbar:          dw br_dl_rs_sbar, 0
br_dl_r_saveas:        dw br_dl_rs_saveas, 0
br_dl_r_follow:        dw br_dl_rs_follow, 0
br_dl_r_srcptr:        dw br_dl_rs_srcptr, 0
br_dl_r_flushbuf:      dw br_dl_rs_flushbuf, 0
br_dl_r_parse:         dw br_dl_rs_parse, 0
br_dl_r_settitle:      dw br_dl_rs_settitle, 0
br_dl_r_targetroute:   dw br_dl_rs_targetroute, 0
br_dl_r_uput:          dw br_dl_rs_uput, 0
br_dl_r_fld:           dw br_dl_rs_fld, 0
br_dl_r_encb:          dw br_dl_rs_encb, 0
br_dl_r_getattr:       dw br_dl_rs_getattr, 0
br_dl_r_henc:          dw br_dl_rs_henc, 0
br_dl_r_status:        dw br_dl_rs_status, 0
br_dl_r_targetgo:      dw br_dl_rs_targetgo, 0
br_dl_r_bmfindurl:     dw br_dl_rs_bmfindurl, 0
br_dl_r_bcopyz:        dw br_dl_rs_bcopyz, 0
br_dl_r_odwrite:       dw br_dl_rs_odwrite, 0
BRDL_RN equ ($ - br_dl_rtab) / 4

br_dl_rs_errst:     call br_errst
                    retf
br_dl_rs_split:     call br_split
                    retf
br_dl_rs_hrefpfx:   call br_hrefpfx
                    retf
br_dl_rs_resolve:   call br_resolve
                    retf
br_dl_rs_urlgrow:   call br_urlgrow
                    retf
br_dl_rs_uibtninit: call os88ui_btninit
                    retf
br_dl_rs_uibtn:                     ; the manager's one button, drawn by part 0
    call os88ui_btn                 ; (tests/btnsites.txt counts THIS site: the
    retf                            ; part's far call is invisible to a grep)
br_dl_rs_uibtnup:   call os88ui_btnup
                    retf
br_dl_rs_uibtndrag: call os88ui_btndrag
                    retf
br_dl_rs_pagepin:   call br_pagepin
                    retf
br_dl_rs_pageunpin: call br_pageunpin
                    retf
br_dl_rs_abort:     call br_abort
                    retf
br_dl_rs_net_find:  call net_find
                    retf
br_dl_rs_copyz:     call br_dl_copyz
                    retf
br_dl_rs_prefix:    call br_dl_prefix
                    retf
br_dl_rs_sockclose: call br_dl_sockclose
                    retf
br_dl_rs_docmap:    call br_docmap
                    retf
br_dl_rs_avail:     call br_avail
                    retf
br_dl_rs_toasturl:                  ; the URL (the bar is 25 cells; the whole
    mov si, br_url                  ; URL lives in br_url for the harness)
    jmp short br_dl_rs_toast
br_dl_rs_toastpost:
    mov si, br_s_posting
br_dl_rs_toast:
    push cx
    push es
    mov cx, 120
    push ds
    pop es
    call OSAPI_TOAST
    pop es
    pop cx
    retf
br_dl_rs_measure:   call br_measure
                    retf
br_dl_rs_hitofs:    call br_hitofs
                    retf
br_dl_rs_linkat:    call br_linkat
                    retf
br_dl_rs_paint_band: call br_paint_band
                    retf
br_dl_rs_sbar:      call br_sbar
                    retf
br_dl_rs_saveas:    call br_saveas
                    retf
br_dl_rs_follow:    call br_follow
                    retf
br_dl_rs_srcptr:    call br_srcptr
                    retf
br_dl_rs_flushbuf:  call br_flushbuf
                    retf
br_dl_rs_parse:     call br_parse
                    retf
br_dl_rs_settitle:  call br_settitle
                    retf
br_dl_rs_targetroute: call br_targetroute
                    retf
br_dl_rs_uput:      call br_uput
                    retf
br_dl_rs_fld:       call br_fld
                    retf
br_dl_rs_encb:      call br_encb
                    retf
br_dl_rs_getattr:   call br_getattr
                    retf
br_dl_rs_henc:      call br_henc
                    retf
br_dl_rs_status:    call br_status
                    retf
br_dl_rs_targetgo:  call br_targetgo
                    retf
br_dl_rs_bmfindurl: call br_bmfindurl
                    retf
br_dl_rs_bcopyz:    call br_copyz
                    retf
br_dl_rs_odwrite:   call od_write
                    retf

br_dl_copyz:
    push ax
    push cx
    push si
    push di
    jcxz .done
    dec cx
.loop:
    lodsb
    or al, al
    jz .term
    jcxz .term
    stosb
    loop .loop
.term:
    xor al, al
    stosb
.done:
    pop di
    pop si
    pop cx
    pop ax
    ret

; SI = parsed URL path. CF=1 when its final filename suffix is not rendered
; by this browser and should go straight to the existing Save/Download flow.
; Query and fragment bytes are excluded, and a dot in a directory is ignored.
br_dl_autosave:
    push ax
    push bx
    push dx                         ; .suffix borrows it (SPEC.md 1: kept)
    push di
    push si
    xor di, di                      ; most recent extension start, if any
.scan:
    lodsb
    or al, al
    jz .suffix
    cmp al, '?'
    je .suffix
    cmp al, '#'
    je .suffix
    cmp al, '/'
    jne .dot
    xor di, di                      ; only the last path component matters
    jmp short .scan
.dot:
    cmp al, '.'
    jne .scan
    mov di, si                      ; first byte after the final dot
    jmp short .scan
.suffix:
    or di, di
    jz .native                      ; extensionless URLs often serve HTML
    mov dx, di                      ; candidate suffix
    mov di, br_dl_exttab
.match:
    mov bx, [di]
    or bx, bx
    jz .download                    ; no renderable suffix matched
    mov si, dx
    call br_dl_extmatch
    jnc .native
    add di, 2
    jmp short .match
.download:
    stc
    jmp short .out
.native:
    clc
.out:
    pop si
    pop di
    pop dx
    pop bx
    pop ax
    ret

; SI = candidate URL suffix, DS:BX = suffix literal. A match consumes the
; complete suffix and requires the URL to end at NUL, query or fragment.
br_dl_extmatch:
    push ax
    push si
    push bx
.next:
    mov al, [bx]
    or al, al
    jz .end
    mov ah, [si]
    cmp ah, 'A'
    jb .compare
    cmp ah, 'Z'
    ja .compare
    or ah, 0x20
.compare:
    cmp ah, al
    jne .no
    inc si
    inc bx
    jmp short .next
.end:
    mov al, [si]
    or al, al
    jz .yes
    cmp al, '?'
    je .yes
    cmp al, '#'
    je .yes
.no:
    pop bx
    pop si
    pop ax
    stc
    ret
.yes:
    pop bx
    pop si
    pop ax
    clc
    ret

; SI string starts with DS:BX prefix, case-insensitive. CF=0 on match.
br_dl_prefix:
    push ax
    push bx
    push si
.next:
    mov al, [bx]
    or al, al
    jz .yes
    mov ah, [si]
    cmp ah, 'A'
    jb .cmp
    cmp ah, 'Z'
    ja .cmp
    or ah, 0x20
.cmp:
    cmp al, ah
    jne .no
    inc bx
    inc si
    jmp .next
.yes:
    pop si
    pop bx
    pop ax
    clc
    ret
.no:
    pop si
    pop bx
    pop ax
    stc
    ret

; The dialog's name pointer is transient. Copy it before directory and disk APIs.
br_dl_rootpath: db '/', 0

; Claim one destination cluster without crossing the load-start 20% reserve.
br_dl_btplace:
    push ax
    push bx
    push dx
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = left, DX = top
    mov [br_dl_x], ax
    mov [br_dl_y], dx
    add ax, [br_dl_w]
    sub ax, 64
    mov [br_dl_btnrects], ax
    add ax, 56
    mov [br_dl_btnrects+4], ax
    add dx, [br_dl_ybutton]
    mov [br_dl_btnrects+2], dx
    add dx, 12
    mov [br_dl_btnrects+6], dx
    pop dx
    pop bx
    pop ax
    ret

br_dl_onclick:
    ret

; UI task callback: only here may the staged cluster reach the filesystem.
br_dl_sockclose:
    push ax
    push bx
    cmp byte [br_dl_hnd], 0
    je .out
    mov al, [br_dl_hnd]
    mov bh, NET_CLASS
    mov bl, NETV_CLOSE
    call OSAPI_DRV_CALL
    mov byte [br_dl_hnd], 0
.out:
    pop bx
    pop ax
    ret

br_dl_s_get: db 'GET ', 0
br_dl_s_http10: db ' HTTP/1.0', 13, 10, 0
br_dl_s_host: db 'Host: ', 0
br_dl_s_agent: db 13, 10, 'User-Agent: os8088 Browser 1.7', 0
br_dl_s_conn: db 13, 10, 'Connection: close', 13, 10, 13, 10, 0

br_dl_s_cl: db 'content-length:', 0
br_dl_s_loc: db 'location:', 0
br_dl_s_te: db 'transfer-encoding:', 0

br_dl_tpl:
    dw 40, 20, 560, 154
    dw br_dl_title, br_dl_paint, 0, 0
br_dl_title: db 'Download Manager', 0

br_dl_btnlabels: dw 0
br_dl_btnrects: times 4 dw 0
br_dl_btncount: dw 1
OS88UI_BTNREC br_dl_btrec, br_dl_btnrects, br_dl_btnlabels, 0, 1, br_dl_btplace
                                ; (the LIVE COUNT is the record's word, written
                                ; with br_dl_btncount by br_dl_button: the
                                ; label's ADDRESS here was a count of ~40,000)
br_dl_btrecn equ br_dl_btrec + OS88UI_BT_N  ; ...named for part 0, which has
                                ; no os88ui.inc to add the field with
