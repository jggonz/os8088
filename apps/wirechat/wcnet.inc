; =============================================================================
; os8088 - apps/wirechat/wcnet.inc
;
; THE WORKER AND THE HTTP/1.0 CLIENT (SPEC.md 113.3, 113.4).
;
; ONE REQUEST, START TO FINISH, AND NOTHING ELSE. The UI task composes the
; whole request - line, headers and body - into wc_reqb (wcproto.inc) and
; calls wc_start; the worker opens the socket, sends wc_reqb, reads the head
; a line at a time and drains the body into wc_resp, then posts WW_DONE or
; WW_FAIL in [wc_wake] and wakes the UI task. It never parses a body, never
; decides what comes next and never draws, which is why its stack class is
; an argument about the driver and not about this file (wirechat.asm).
;
; IT IS THE WIRE'S CLIENT, NOT ITS COPY. apps/thewire/wrhttp.inc reads a body
; of a KNOWN size into one of four destinations and GETs; this POSTs, reads a
; body of any size up to a ceiling into one buffer, and is aborted from the
; other task as an ordinary part of every message sent (SPEC.md 113.3). The
; parts that are the same - the state names, the generation, the line-at-a-
; time header read - are the same on purpose, so a reader of one can read
; the other; docs/WIRE-PLAN.md 11's shared apps/os88http.inc would take both
; shapes as parameters, and two shapes is what it was waiting for.
;
; **THE GENERATION IS BUMPED FIRST AND THE SOCKET CLOSED SECOND** (wrhttp.inc's
; division, SPEC.md 71.11): only the worker opens, so between the UI task's
; close and the next socket there is a pass boundary, and at it wc_gchk turns
; the superseded pass into nothing.
; =============================================================================

; -----------------------------------------------------------------------------
; wc_hire - one worker, retried from every paint until granted (SPEC.md 20.6)
; in:  the gfx lock HELD (a spawn wants it, and an entry proc may not spawn)
; out: nothing; every register preserved
; -----------------------------------------------------------------------------
wc_hire:
    cmp byte [wc_hired], 1
    je .out
    push ax
    push bx
    mov ax, wc_worker
    mov bx, [wc_win]
    call OSAPI_TASK_SPAWN               ; CF = 1: the table is full, which is
    jc .no                              ; transient - the next paint asks again
    mov byte [wc_hired], 1
    OS88_WORKER_RESTARTABLE_NOKEEP wc_worker  ; SPEC.md 66.6.2: the park is in
                                        ; OSAPI_TASK_ALIVE at the top of the
                                        ; loop and every byte is a static
.no:
    pop bx
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; wc_start - run the request in wc_reqb ([wc_reqn] bytes)
; in:  the UI task
; out: nothing; every register preserved. The worker picks it up next tick.
; -----------------------------------------------------------------------------
wc_start:
    push ax
    push cx
    push di
    push es
    call wc_abandon                     ; bump, close: whatever was running
    push ds                             ; is superseded from here
    pop es
    mov di, wc_code                     ; wc_code .. wc_rxn are one run of the
    mov cx, (wc_rxn + 2 - wc_code) / 2  ; bss: zero them in one store
    xor ax, ax
    rep stosw
    mov [wc_wake], al                   ; WW_NONE
    mov [wc_nodrv], al
    mov byte [wc_state], WS_OPEN
    pop es
    pop di
    pop cx
    pop ax
    ret
%if (wc_rxn + 2 - wc_code) & 1
  %error "wc_start zeroes wc_code..wc_rxn as words: keep the run even"
%endif

; -----------------------------------------------------------------------------
; wc_abandon - drop whatever is in flight. in: the UI task. Preserves all.
; -----------------------------------------------------------------------------
wc_abandon:
    inc byte [wc_gen]
    mov byte [wc_state], WS_IDLE
    jmp short wc_hclose

; --- wc_hclose - give the handle back, whatever happened; preserves all -------
wc_hclose:
    push ax
    push bx
    xor al, al
    xchg al, [wc_hnd]                   ; the handle, and NONE from this one
    or al, al                           ; instruction on, so the other task's
    jz .out                             ; close cannot close it twice
    mov bh, NET_CLASS
    mov bl, NETV_CLOSE
    call OSAPI_DRV_CALL
.out:
    pop bx
    pop ax
    ret

; --- wc_gchk - CF = 1 if the UI task started something else under this pass.
; Preserves every register INCLUDING AX, which .open needs.
wc_gchk:
    push ax
    mov al, [wc_gen]
    cmp al, [wc_gen0]
    pop ax
    je .ok
    stc
    ret
.ok:
    clc
    ret

; =============================================================================
; THE WORKER (SPEC.md 20.6)
; =============================================================================
wc_worker:
.loop:
    mov bx, [wc_win]
    call OSAPI_TASK_ALIVE               ; the death door: may never return
    call wc_nstep
    mov ax, 1
    call OSAPI_TASK_SLEEP
    jmp .loop

wc_nstep:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es                              ; ES = OURS for every NETV_* buffer
    mov al, [wc_gen]                    ; bank the generation: this pass may be
    mov [wc_gen0], al                   ; pre-empted inside any call below
    mov al, [wc_state]
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
    call net_find                       ; which driver, and NSTF_SOCK
    jc .nodrv
    jz .nodrv
    mov bh, NET_CLASS
    mov bl, NETV_OPEN
    mov si, wc_host
    mov cx, [wc_port]
    call OSAPI_DRV_CALL
    jc .retry
    call wc_gchk                        ; preserves AX: the new handle
    jc .stale
    mov [wc_hnd], al
    mov byte [wc_state], WS_WAIT
    jmp .out
.stale:
    mov [wc_hnd], al                    ; superseded while opening: nobody's
    call wc_hclose
    jmp .out
.nodrv:
    call wc_gchk
    jc .out
    mov byte [wc_nodrv], 1
    jmp .fail

.wait:
    call wc_qstat
    jc .retry
    cmp ah, NSK_CONNECT
    je .out
    cmp ah, NSK_UP
    jne .fail                           ; refused, failed, closed
    call wc_gchk
    jc .out
    mov byte [wc_state], WS_SEND
    jmp .out

.send:
    mov si, wc_reqb
    add si, [wc_sent]
    mov cx, [wc_reqn]
    sub cx, [wc_sent]
    cmp cx, NET_SKMAX                   ; one verb carries at most this
    jbe .sendn
    mov cx, NET_SKMAX
.sendn:
    mov al, [wc_hnd]
    mov bh, NET_CLASS
    mov bl, NETV_SEND
    call OSAPI_DRV_CALL
    jc .retry
    call wc_gchk
    jc .out
    add [wc_sent], cx                   ; it may take FEWER than asked, and 0
    mov ax, [wc_sent]                   ; is ordinary
    cmp ax, [wc_reqn]
    jb .out
    mov byte [wc_state], WS_HEAD
    jmp .out

.drain:
    mov al, [wc_hnd]
    mov di, wc_rxb
    mov cx, WC_CHUNK
    mov bh, NET_CLASS
    mov bl, NETV_RECV
    call OSAPI_DRV_CALL
    jc .retry
    call wc_gchk                        ; the UI task may have moved on
    jc .out
    jcxz .maybeend
    mov [wc_rxn], cx
    mov word [wc_rxp], 0
    call wc_take
    cmp byte [wc_state], WS_FAIL
    je .fail
    cmp byte [wc_state], WS_BODY
    jne .out
    mov ax, [wc_got]                    ; the whole body? clen fits 16 bits:
    cmp ax, [wc_clen]                   ; wc_hdrdone refused anything over
    jb .out                             ; WC_RESPMAX
    call wc_finish
    jmp short .out
.maybeend:
    call wc_qstat                       ; nothing read is NOT an end
    jc .retry                           ; (netpkg.inc): the end is CLOSING
    cmp ah, NSK_UP                      ; with nothing behind it
    je .out
    cmp ah, NSK_CONNECT
    je .out
    jcxz .short
    jmp short .out
.short:
    call wc_gchk                        ; the peer closed before its own
    jc .out                             ; Content-Length was met
    jmp short .fail
.retry:
    cmp ax, NETE_BUSY
    je .out                             ; the wire is busy: next tick, not a
                                        ; failure (netpkg.inc)
.fail:
    call wc_gchk                        ; a failure belongs to the request
    jc .out                             ; that had it
    call wc_failpost
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- wc_qstat - NETV_STATUS on our handle. out AH = state, CX = readable -----
wc_qstat:
    mov al, [wc_hnd]
    mov bh, NET_CLASS
    mov bl, NETV_STATUS
    call OSAPI_DRV_CALL                 ; a CALL and a ret, never a jmp: the
    ret                                 ; slot is far and returns far

; -----------------------------------------------------------------------------
; wc_take - the [wc_rxn] bytes in wc_rxb: the head a line at a time, then the
;           body into wc_resp
; out: [wc_state] may become WS_BODY or WS_FAIL; clobbers AX, CX, SI
;
; THE GENERATION IS RE-READ PER BYTE (SPEC.md 71.11, wrhttp.inc's reason): a
; tick inside a thousand-byte loop lets the UI task abandon this request and
; compose the next one, and the rest of the chunk belongs to nobody.
; -----------------------------------------------------------------------------
wc_take:
    push bx
    mov si, wc_rxb
    mov cx, [wc_rxn]
.next:
    jcxz .done
    mov ah, [wc_gen]
    cmp ah, [wc_gen0]
    jne .done
    lodsb
    dec cx
    cmp byte [wc_state], WS_BODY
    je .body
    call wc_hdrb
    cmp byte [wc_state], WS_FAIL
    jne .next
    jmp short .done
.body:
    mov bx, [wc_got]
    cmp bx, [wc_clen]                   ; more than it said it would send: the
    jae .over                           ; server is not believed about its own
    mov [wc_resp+bx], al                ; length
    inc word [wc_got]
    jmp short .next
.over:
    mov byte [wc_state], WS_FAIL
.done:
    mov word [wc_rxn], 0
    pop bx
    ret

; --- wc_hdrb - one byte of the head. in: AL. clobbers BX -------------------
wc_hdrb:
    cmp al, 13
    je .out                             ; CR is dropped; LF ends the line
    cmp al, 10
    je wc_hdrline
    mov bx, [wc_hlen]
    cmp bx, WC_HLINE - 1                ; a long line is CLIPPED, which cannot
    jae .out                            ; lose a length: 'Content-Length:
    mov [wc_hbuf+bx], al                ; 6144' is 20 bytes
    inc word [wc_hlen]
.out:
    ret

; -----------------------------------------------------------------------------
; wc_hdrline - a whole header line is in wc_hbuf ([wc_hlen] bytes)
; Line 0 is the status: the code is columns 9..11 (br_hdrb's scan, done once).
; An empty line ends the head. 'Content-Length:' is matched case-blind.
; clobbers BX
; -----------------------------------------------------------------------------
wc_hdrline:
    push ax
    push cx
    push dx
    push si
    push di
    mov bx, [wc_hlen]
    mov byte [wc_hbuf+bx], 0
    cmp word [wc_hline], 0
    jne .field
    cmp bx, 12                          ; 'HTTP/1.x NNN' at the least, or the
    jb .next                            ; code stays 0 and the reply fails
    mov si, wc_hbuf + 9
    call wc_dec32                       ; AX = the code (three digits, so
    mov [wc_code], ax                   ; DX is 0)
    jmp short .next
.field:
    or bx, bx
    jz .end
    mov si, wc_hbuf
    mov di, wc_s_clen
    call wc_pfxci                       ; CF = 0: it starts with that
    jc .next
    call wc_dec32                       ; SI is past the colon; spaces skipped
    mov [wc_clen], ax
    mov [wc_clen+2], dx
    mov byte [wc_haslen], 1
    jmp short .next
.end:
    call wc_hdrdone
.next:
    inc word [wc_hline]
    mov word [wc_hlen], 0
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; --- wc_hdrdone - the head is complete: is this a reply we will read? -----
; SPEC.md 113.2: every answer is a 200 with a length, success or refusal
; alike, so anything else means the server is not WireChat - a proxy's error
; page, a captive portal - and the request has failed.
wc_hdrdone:
    cmp word [wc_code], 200
    jne .bad
    cmp byte [wc_haslen], 0
    je .bad
    cmp word [wc_clen+2], 0
    jne .bad
    cmp word [wc_clen], WC_RESPMAX
    ja .bad
    mov byte [wc_state], WS_BODY
    cmp word [wc_clen], 0               ; an empty body is complete already;
    jne .out                            ; wc_nstep finishes it on got >= clen,
    call wc_finish                      ; which needs a byte to look at
.out:
    ret
.bad:
    mov byte [wc_state], WS_FAIL
    ret

; --- the two ways a request ends ------------------------------------------
; Written-last-then-wake: the UI task reads [wc_wake] and then everything it
; names, so the byte goes in after the state it describes.
wc_finish:
    push bx
    call wc_hclose
    mov bx, [wc_got]
    mov byte [wc_resp+bx], 0            ; NUL-terminated for the parser
    mov byte [wc_state], WS_DONE
    mov byte [wc_wake], WW_DONE
    jmp short wc_post

wc_failpost:
    push bx
    call wc_hclose
    mov byte [wc_state], WS_FAIL
    mov byte [wc_wake], WW_FAIL
wc_post:
    mov bx, [wc_win]
    call OSAPI_WM_WAKE
    pop bx
    ret

; -----------------------------------------------------------------------------
; wc_dec32 - a decimal number at DS:SI, leading spaces skipped
; out: DX:AX = the value (saturating at 0xFFFFFFFF), SI past its last digit;
;      clobbers BX, CX
; -----------------------------------------------------------------------------
wc_dec32:
.sp:
    cmp byte [si], ' '
    jne .go
    inc si
    jmp short .sp
.go:
    xor ax, ax
    xor dx, dx
.dig:
    mov bl, [si]
    sub bl, '0'
    cmp bl, 9
    ja .out
    inc si
    push bx                             ; DX:AX = DX:AX * 10 + digit
    mov cx, ax
    mov bx, dx
    shl ax, 1                           ; *2
    rcl dx, 1
    jc .sat
    shl ax, 1                           ; *4
    rcl dx, 1
    jc .sat
    add ax, cx                          ; *5
    adc dx, bx
    jc .sat
    shl ax, 1                           ; *10
    rcl dx, 1
    jc .sat
    pop bx
    xor bh, bh
    add ax, bx
    adc dx, 0
    jnc .dig
    push bx
.sat:
    pop bx
    mov ax, 0xFFFF
    mov dx, ax
.skip:
    mov bl, [si]                        ; the rest of the digits are spent
    sub bl, '0'
    cmp bl, 9
    ja .out
    inc si
    jmp short .skip
.out:
    ret

; -----------------------------------------------------------------------------
; wc_pfxci - does the NUL string at SI begin with the NUL string at DI,
;            ignoring the case of letters?
; out: CF = 0 yes, and SI is past the prefix; CF = 1 no, SI unchanged.
;      clobbers AX, DI
; -----------------------------------------------------------------------------
wc_pfxci:
    push si
.c:
    mov ah, [di]
    or ah, ah
    jz .yes
    mov al, [si]
    call wc_lower
    xchg al, ah
    call wc_lower
    cmp al, ah
    jne .no
    inc si
    inc di
    jmp short .c
.yes:
    add sp, 2                           ; drop the saved SI: the caller wants
    clc                                 ; it advanced
    ret
.no:
    pop si
    stc
    ret

; --- wc_lower - AL to lower case if it is a letter; preserves the rest -----
wc_lower:
    cmp al, 'A'
    jb .out
    cmp al, 'Z'
    ja .out
    or al, 0x20
.out:
    ret
