; =============================================================================
; os8088 - apps/thewire/wrhttp.inc
;
; THE WIRE'S HTTP/1.0 CLIENT (SPEC.md 88.4), and the worker that drives it.
;
; --- WRITTEN SO IT COULD MOVE TO apps/ -------------------------------------
; docs/WIRE-PLAN.md 11 defers a shared `apps/os88http.inc` and says why: the
; browser's client is entangled with the browser's bss, so a third consumer
; is what should pay for the generalisation. This one is kept at arm's length
; from the rest of the package on purpose - it touches exactly five things
; outside itself, and they are the ones an include would take as arguments:
;
;   [wr_host] [wr_port] [wr_path]   what to ask for
;   [wr_dseg] [wr_dbase] [wr_dmax]  where the body goes
;   [wr_state] [wr_kind] [wr_got]   what is happening
;   [wr_gen] [wr_gen0]              the generation (SPEC.md 71.11)
;   wr_finish / wr_failpost         the two ways it ends
;
; Everything else here is private. The one deliberate coupling is the
; INVERSION in wr_take - a WK_PIC body is stored `not`-ed, which is SPEC.md
; 88.3's whole adapter story - and it is one instruction behind one compare.
;
; --- THE DESTINATION IS A FAR POINTER, WHICH THE BROWSER'S IS NOT ------------
; brnet.inc always drains into one claim. This drains into a claim (a catalog
; or a package) or into the package's OWN bss (a picture), so the destination
; is a segment and an offset rather than a segment. That is the only
; structural difference between the two clients.
;
; --- HEADERS ARE READ A LINE AT A TIME --------------------------------------
; brnet.inc's four-state CR-LF-CR-LF machine never looks at a header's
; CONTENTS, because the browser does not need one. This needs
; `Content-Length` - it bounds the body, it drives the progress figure, and
; without it the transfer cannot be checked at all - so it has to buffer a
; line anyway, and a line buffer is then both smaller and the thing that makes
; the length readable. A line longer than WR_HLINE is CLIPPED, which cannot
; lose a length header: `Content-Length: 65535` is 21 bytes.
; =============================================================================

; -----------------------------------------------------------------------------
; wr_hire - one worker, retried until granted (SPEC.md 20.6)
; A full task table is a NORMAL, TRANSIENT outcome, so this is called from
; every paint and latches only on success - br_hire's shape exactly.
; -----------------------------------------------------------------------------
wr_hire:
    push ax
    push bx
    cmp byte [wr_hired], 1
    je .out
    mov ax, wr_worker
    mov bx, [wr_win]
    or bx, bx
    jz .out
    call OSAPI_TASK_SPAWN
    jc .out
    mov byte [wr_hired], 1
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wr_start - begin a transfer
; in:  AX = the destination segment, BX = its offset, CX = the capacity there,
;      DX = a WK_* kind. THE UI TASK.
; out: nothing; the worker picks it up on its next pass
;
; **THE GENERATION IS BUMPED FIRST AND THE SOCKET CLOSED SECOND**, which is
; br_abort's division and its safety argument: only the worker calls
; NETV_OPEN, it is one task, so between this close and the next socket there
; is at least one pass boundary - and at that boundary wr_gchk turns the
; superseded pass into a no-op. A call already dispatched against the old
; handle lands on a socket that is closed and not yet reissued.
; -----------------------------------------------------------------------------
wr_start:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    inc byte [wr_gen]
    call wr_hclose
    mov [wr_dseg], ax
    mov [wr_dbase], bx
    mov [wr_dmax], cx
    mov [wr_kind], dl
    mov word [wr_got], 0
    mov word [wr_clen], 0
    mov word [wr_code], 0
    mov word [wr_hlen], 0
    mov word [wr_hline], 0
    mov word [wr_sent], 0
    mov word [wr_msg], 0
    mov word [wr_shown], 0xFFFF
    mov byte [wr_wake], WW_NONE
    call wr_pathbuild
    mov byte [wr_state], WS_OPEN
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- wr_pathbuild - the prefix and what this kind asks for, into wr_path -----
wr_pathbuild:
    push ax
    push cx
    push si
    push di
    push es
    mov di, wr_path
    mov si, wr_pfx
    call wr_sput
    mov al, [wr_kind]
    cmp al, WK_CAT
    je .cat
    cmp al, WK_PIC
    je .pic
    mov si, wr_p_pkg                    ; /wire/pkg/<NAME>
    call wr_sput
    mov si, wr_fname
    call wr_sput
    jmp short .term
.cat:
    mov si, wr_f_cat                    ; /wire/catalog.bin
    call wr_sput
    jmp short .term
.pic:
    mov si, wr_p_pic                    ; /wire/pic/<STEM>.PIC
    call wr_sput
    mov ax, [wr_sel]
    call wr_recs
    add si, WC_STEM
    mov cx, 8
    call wr_stemcopy
    mov si, wr_x_pic
    call wr_sput
.term:
    mov byte [di], 0
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; --- wr_hclose - give the handle back, whatever happened ---------------------
wr_hclose:
    push ax
    push bx
    cmp byte [wr_hnd], 0
    je .out
    mov al, [wr_hnd]
    mov bh, NET_CLASS
    mov bl, NETV_CLOSE
    call OSAPI_DRV_CALL
    mov byte [wr_hnd], 0
.out:
    pop bx
    pop ax
    ret

; --- wr_gchk - CF = 1 if the UI task started another transfer under this pass
; Preserves every register INCLUDING AX, which .open needs: the handle it is
; about to store is sitting in it.
wr_gchk:
    push ax
    mov al, [wr_gen]
    cmp al, [wr_gen0]
    je .ok
    pop ax
    stc
    ret
.ok:
    pop ax
    clc
    ret

; =============================================================================
; THE WORKER (SPEC.md 20.6)
; =============================================================================
wr_worker:
.loop:
    mov bx, [wr_win]
    call OSAPI_TASK_ALIVE               ; the death door: may never return
    call wr_nstep
    mov ax, 1
    call OSAPI_TASK_SLEEP
    jmp .loop

wr_nstep:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es                              ; ES = OURS for every NETV_* buffer
                                        ; (netpkg.inc; SPEC.md 77.10 is what a
                                        ; buffer in a claim costs)
    mov al, [wr_gen]                    ; **BANK THE GENERATION.** This pass may
    mov [wr_gen0], al                   ; be pre-empted inside any driver call
                                        ; below, and while it is the UI task can
                                        ; drop this transfer and start another
    mov al, [wr_state]
    cmp al, WS_OPEN
    je .open
    cmp al, WS_WAIT
    je .wait
    cmp al, WS_SEND
    je .send
    cmp al, WS_HEAD
    je .drain
    cmp al, WS_BODY
    je .drain
    jmp .out

.open:
    call net_find                       ; WHICH driver, and is it one of ours?
    jc .nodrv                           ; (SPEC.md 20.11.1 - a class is a
    mov bh, NET_CLASS                   ; publication slot, not a driver)
    mov bl, NETV_STATE
    call OSAPI_DRV_CALL
    jc .nodrv
    test al, NSTF_SOCK
    jz .nodrv
    mov bh, NET_CLASS
    mov bl, NETV_OPEN
    mov si, wr_host
    mov cx, [wr_port]
    call OSAPI_DRV_CALL
    jc .retry
    call wr_gchk                        ; ...and it preserves AX, which is the
    jc .stale                           ; handle we have just been given
    mov [wr_hnd], al
    mov byte [wr_state], WS_WAIT
    jmp .redraw
.stale:                                 ; superseded WHILE OPENING: this socket
    mov bh, NET_CLASS                   ; belongs to nobody, and leaving it
    mov bl, NETV_CLOSE                  ; would hold a handle for the session
    call OSAPI_DRV_CALL
    jmp .out
.nodrv:
    mov si, wr_s_nodrv
    jmp .fail

.wait:
    call wr_qstat
    jc .retry
    cmp ah, NSK_UP
    je .up
    cmp ah, NSK_CONNECT
    je .out
    mov si, wr_s_refus
    jmp .fail
.up:
    call wr_gchk
    jc .out
    call wr_req
    mov word [wr_sent], 0
    mov byte [wr_state], WS_SEND
    jmp .redraw

.send:
    mov si, wr_reqb
    add si, [wr_sent]
    mov cx, [wr_reqn]
    sub cx, [wr_sent]
    mov al, [wr_hnd]
    mov bh, NET_CLASS
    mov bl, NETV_SEND
    call OSAPI_DRV_CALL
    jc .retry
    call wr_gchk
    jc .out
    add [wr_sent], cx                   ; it may take FEWER than asked, and 0
    mov ax, [wr_sent]                   ; is ordinary: advance by what it took
    cmp ax, [wr_reqn]
    jb .out
    mov byte [wr_state], WS_HEAD
    jmp .redraw

.drain:
    mov al, [wr_hnd]
    mov di, wr_rxb
    mov cx, WR_CHUNK
    mov bh, NET_CLASS
    mov bl, NETV_RECV
    call OSAPI_DRV_CALL
    jc .retry
    call wr_gchk                        ; **THE ONE THAT MATTERS**: the UI task
    jc .out                             ; may have freed and re-claimed the
    or cx, cx                           ; destination under this pass
    jz .maybeend
    call wr_take
    cmp byte [wr_state], WS_FAIL
    je .postfail
    cmp byte [wr_state], WS_BODY
    jne .prog
    mov ax, [wr_got]
    cmp ax, [wr_clen]
    jb .prog
    call wr_finish
    jmp short .out
.prog:
    call wr_progress
    jmp short .out
.postfail:
    call wr_failpost
    jmp short .out
.maybeend:
    call wr_qstat                       ; NOTHING READ IS NOT AN END
    jc .retry                           ; (netpkg.inc): the end is NSK_CLOSING
    cmp ah, NSK_UP                      ; with nothing left behind it
    je .out
    cmp ah, NSK_CONNECT
    je .out
    or cx, cx
    jnz .out
    call wr_gchk
    jc .out
    mov si, wr_s_lost                   ; the peer closed before its own
    jmp short .fail                     ; Content-Length was met: a short
                                        ; answer is a failed transfer, not a
                                        ; small one
.retry:
    cmp ax, NETE_BUSY
    je .out                             ; the wire is busy: next tick, and NOT
    mov si, wr_s_lost                   ; a failure (netpkg.inc)
.fail:
    call wr_gchk                        ; a failure belongs to the transfer
    jc .out                             ; that had it, and this one is dropped
    mov [wr_msg], si
    call wr_failpost
    jmp short .out
.redraw:
    call wr_nshow
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- wr_qstat - NETV_STATUS on our handle. out AH = state, CX = readable -----
wr_qstat:
    mov al, [wr_hnd]
    mov bh, NET_CLASS
    mov bl, NETV_STATUS
    call OSAPI_DRV_CALL
    ret

; -----------------------------------------------------------------------------
; wr_req - compose the request into wr_reqb, and set [wr_reqn]
;
; HTTP/1.0 and `Connection: close`, which together are the whole reason this
; needs no chunked decoder.
; -----------------------------------------------------------------------------
wr_req:
    push ax
    push bx
    push si
    push di
    mov di, wr_reqb
    mov bx, wr_reqb + WR_REQMAX - 1     ; the last byte wr_rputs may write, set
                                        ; ONCE for all five pieces: WR_REQMAX
                                        ; is derived from them and this is what
                                        ; keeps a longer User-Agent from
                                        ; quietly making it wrong again
    mov si, wr_s_get
    call wr_rputs
    mov si, wr_path
    call wr_rputs
    mov si, wr_s_http
    call wr_rputs
    mov si, wr_host
    call wr_rputs
    mov si, wr_s_tail
    call wr_rputs
    sub di, wr_reqb
    mov [wr_reqn], di
    pop di
    pop si
    pop bx
    pop ax
    ret

; --- wr_rputs - the NUL string at DS:SI to DS:DI, DI advanced, BX-bounded ----
wr_rputs:
    push ax
.c:
    mov al, [si]
    or al, al
    jz .out
    cmp di, bx
    ja .out
    mov [di], al
    inc di
    inc si
    jmp short .c
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; wr_take - CX bytes at wr_rxb into the destination
;
; **THE GENERATION IS RE-READ PER BYTE** (SPEC.md 71.11). wr_nstep checks once
; a pass and this loop is up to WR_CHUNK stores long: a tick landing inside it
; lets the UI task drop the transfer and start another, and the rest of the
; chunk would then be stored into a claim that has been freed - or, worse,
; re-claimed by somebody else. The check at the top of the pass was true when
; the pass started and is stale a thousand stores later.
; -----------------------------------------------------------------------------
wr_take:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov si, wr_rxb
    mov es, [wr_dseg]                   ; ONCE, not per byte: nothing in the
                                        ; loop but the store touches ES, and
                                        ; DS is untouched throughout
.next:
    mov ah, [wr_gen]
    cmp ah, [wr_gen0]
    jne .done
    mov al, [si]
    inc si
    cmp byte [wr_state], WS_BODY
    je .body
    call wr_hdrb
    cmp byte [wr_state], WS_FAIL
    je .done
    jmp short .step
.body:
    mov bx, [wr_got]
    cmp bx, [wr_clen]
    jae .over
    cmp bx, [wr_dmax]
    jae .over
    cmp byte [wr_kind], WK_PIC
    jne .st
    not al                              ; **THE PICTURE IS INVERTED AS IT
                                        ; ARRIVES** (SPEC.md 88.3): the file's
                                        ; 1 is ink and gfx_blit1 wants a set
                                        ; bit to be a LIT pixel, so one `not`
                                        ; here buys a draw with no adapter
                                        ; branch, no pen call and no second
                                        ; buffer
.st:
    add bx, [wr_dbase]
    mov [es:bx], al
    inc word [wr_got]
.step:
    loop .next
.done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.over:
    ; MORE BODY THAN IT CLAIMED. The server is not to be believed about its own
    ; length, so the transfer is failed rather than silently truncated: a
    ; catalog cut at the ceiling parses as a shorter catalog and a package cut
    ; at it is an image that will not run.
    mov word [wr_msg], wr_s_lost
    mov byte [wr_state], WS_FAIL
    jmp short .done

; -----------------------------------------------------------------------------
; wr_hdrb - one reply byte, while the headers are still arriving
;
; CR is dropped, LF ends a line, and an EMPTY line ends the headers. The line
; is clipped at WR_HLINE-1 and the count keeps rising, so a very long header
; is truncated rather than overrunning and an empty line is still empty.
; -----------------------------------------------------------------------------
wr_hdrb:
    push ax
    push bx
    push di
    cmp al, 13
    je .out
    cmp al, 10
    je .eol
    mov di, [wr_hlen]
    cmp di, WR_HLINE - 1
    jae .count
    add di, wr_hbuf
    mov [di], al
.count:
    inc word [wr_hlen]
    jmp short .out
.eol:
    mov di, [wr_hlen]
    cmp di, WR_HLINE - 1
    jbe .cap
    mov di, WR_HLINE - 1
.cap:
    add di, wr_hbuf
    mov byte [di], 0
    cmp word [wr_hlen], 0
    je .blank
    call wr_hdrline
    mov word [wr_hlen], 0
    inc word [wr_hline]
    jmp short .out
.blank:
    call wr_hdrdone
.out:
    pop di
    pop bx
    pop ax
    ret

; --- wr_hdrline - one complete header line, NUL-terminated in wr_hbuf --------
wr_hdrline:
    push ax
    push bx
    push cx
    push dx                             ; both accumulators below `mul`, and
    push si                             ; the caller's DX is wr_take's
    push di
    cmp word [wr_hline], 0
    je .status
    mov si, wr_hbuf                     ; `Content-Length: NNN`, case-blind
    mov di, wr_s_clen
    call wr_pfxci
    jc .out
.sp:
    cmp byte [si], ' '
    jne .num
    inc si
    jmp short .sp
.num:
    xor bx, bx
.d:
    mov al, [si]
    cmp al, '0'
    jb .store
    cmp al, '9'
    ja .store
    sub al, '0'
    mov cl, al
    xor ch, ch
    mov ax, bx
    mov bx, 10
    mul bx
    or dx, dx                           ; a length that will not fit a word is
    jnz .huge                           ; refused here rather than wrapping
    add ax, cx
    jc .huge
    mov bx, ax
    inc si
    jmp short .d
.huge:
    mov bx, 0xFFFF                      ; ...and wr_hdrdone refuses it against
.store:                                 ; [wr_dmax], which no claim reaches
    mov [wr_clen], bx
    jmp short .out
.status:
    mov si, wr_hbuf                     ; `HTTP/1.0 200 OK`: columns 9..11
    xor bx, bx
    mov cx, 9
.skip:
    cmp byte [si], 0
    je .out
    inc si
    loop .skip
    mov cx, 3
.code:
    mov al, [si]
    cmp al, '0'
    jb .cdone
    cmp al, '9'
    ja .cdone
    sub al, '0'
    push ax
    mov ax, bx
    mov bx, 10
    mul bx
    mov bx, ax
    pop ax
    xor ah, ah
    add bx, ax
    inc si
    loop .code
.cdone:
    mov [wr_code], bx
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- wr_pfxci - does DS:SI start with the NUL prefix at DS:DI? (case-blind) --
; out: CF = 0 yes, and SI IS ADVANCED past it (which br_pfx deliberately does
;      not do - there the caller decides by how much, and here there is one
;      caller and one answer). CF = 1 no, SI unchanged.
wr_pfxci:
    push ax
    push bx
    push si
.c:
    mov bl, [di]
    or bl, bl
    jz .yes
    mov al, [si]
    or al, al
    jz .no
    cmp al, 'A'
    jb .cmp
    cmp al, 'Z'
    ja .cmp
    add al, 32
.cmp:
    cmp al, bl
    jne .no
    inc si
    inc di
    jmp short .c
.yes:
    add sp, 2                           ; the banked SI is DISCARDED: a match
    pop bx                              ; hands the caller the byte AFTER the
    pop ax                              ; prefix, which is where the value is
    clc
    ret
.no:
    pop si
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; wr_hdrdone - the blank line: is this an answer we can use?
;
; Everything a server says about itself is checked HERE, before one body byte
; is stored. A 200 with no length, a length past the claim, or a picture that
; is not 1,024 bytes are all refusals with a sentence rather than a partial
; transfer with none.
; -----------------------------------------------------------------------------
wr_hdrdone:
    push ax
    push bx
    push si                             ; SI is wr_take's READ POINTER and the
    push di                             ; two message paths below both use it
    cmp word [wr_code], 200
    jne .status
    cmp word [wr_clen], 0
    je .status                          ; no length is 'did not answer (0)',
                                        ; which is true and is what SPEC.md
                                        ; 88.4 requires
    mov ax, [wr_clen]
    cmp ax, [wr_dmax]
    ja .toobig
    cmp byte [wr_kind], WK_CAT
    je .ok                              ; a catalog is the ONE body whose size
                                        ; is not known in advance - the claim
                                        ; is made before any header arrives
    cmp ax, [wr_dmax]                   ; **A PACKAGE AND A PICTURE MUST BE
    jne .badsize                        ; EXACTLY WHAT WAS PROMISED.** The
                                        ; catalog declared WC_SIZE and the
                                        ; format fixes a picture at 1,024, so
                                        ; a short body is a server that
                                        ; disagrees with the catalog - and
                                        ; accepting one would put the claim's
                                        ; uninitialised tail into a file on
                                        ; the user's disk, or into an image
                                        ; handed to the loader
.ok:
    mov byte [wr_state], WS_BODY
    jmp short .out
.status:
    mov di, wr_nmsg
    mov si, wr_s_answ
    call wr_sput
    mov ax, [wr_code]
    call wr_snum
    mov byte [di], ')'
    inc di
    mov byte [di], 0
    mov word [wr_msg], wr_nmsg
    jmp short .fail
.toobig:
    mov word [wr_msg], wr_s_toobig
    jmp short .fail
.badsize:
    mov word [wr_msg], wr_s_badpic
    cmp byte [wr_kind], WK_PIC
    je .fail
    mov word [wr_msg], wr_s_lost
.fail:
    mov byte [wr_state], WS_FAIL
.out:
    pop di
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wr_finish / wr_failpost - the two ways a transfer ends
;
; Both are the WORKER's half of SPEC.md 88.5's one byte: everything the
; handler will read is written first, and [wr_wake] LAST.
; -----------------------------------------------------------------------------
wr_finish:
    push ax
    push bx
    call wr_hclose
    mov ax, [wr_got]
    mov [wr_catlen], ax                 ; only the catalog reads it, and it is
                                        ; free to write for every kind
    mov byte [wr_state], WS_DONE
    mov al, [wr_kind]
    mov [wr_wkind], al
    mov byte [wr_wake], WW_DONE
    mov bx, [wr_win]
    call OSAPI_WM_WAKE
    call wr_nshow
    pop bx
    pop ax
    ret

wr_failpost:
    push ax
    push bx
    call wr_hclose
    mov byte [wr_state], WS_FAIL
    mov al, [wr_kind]
    mov [wr_wkind], al
    mov byte [wr_wake], WW_FAIL
    mov bx, [wr_win]
    call OSAPI_WM_WAKE
    call wr_nshow
    pop bx
    pop ax
    ret

; --- wr_progress - the status cell, but ONLY when the K figure moved ---------
; A repaint per NETV_RECV would be one 48-cell run per kilobyte, which is
; 384px of text about forty times for a 40KB package. The figure the cell
; shows is in KILOBYTES, so redrawing when the KILOBYTE changes is redrawing
; exactly when the picture changes (PERFORMANCE.md rule 1).
wr_progress:
    push ax
    push cx
    mov ax, [wr_got]
    mov cl, 10
    shr ax, cl
    cmp ax, [wr_shown]
    je .out
    mov [wr_shown], ax
    call wr_nshow
.out:
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wr_nshow - the worker's own status repaint, under the lock
;
; SPEC.md 20.6 rule 5: a background painter asks whether it can be SEEN and
; skips the draw when it cannot, because drawing at absolute coordinates over
; a window that is on top of ours is the failure that rule exists for. The
; status cell is ONE opaque font_run, which cannot produce SPEC.md 11.3's
; granularity failure, so the obscured test is all it owes.
; -----------------------------------------------------------------------------
wr_nshow:
    push ax
    push bx
    push si
    call OSAPI_GFX_LOCK
    mov bx, [wr_win]
    call OSAPI_WM_OBSCURED
    jc .un                              ; covered: the next paint owes it
    call wr_geom
    jc .un
    call wr_dstat
.un:
    call OSAPI_GFX_UNLOCK
    pop si
    pop bx
    pop ax
    ret
