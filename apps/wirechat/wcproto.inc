; =============================================================================
; os8088 - apps/wirechat/wcproto.inc
;
; THE WIRE FORMAT, BOTH WAYS (SPEC.md 113.2): wc_compose writes a request
; into wc_reqb, and wc_reply reads the answer out of wc_resp into the
; stores. Both run on the UI task - the composer from the conductor, the
; parser from the wake handler - so neither shares a byte with the worker
; except through wc_start and [wc_wake].
;
; THE PARSER WRITES NULS INTO THE REPLY AS IT GOES: each line's LF becomes a
; NUL, and each field but the last is cut off by one, so a field is a plain
; string to every helper here. The buffer is the worker's again the moment
; the next request starts, so nothing is lost by it.
;
; A RECORD'S FIELDS ARE PARSED INTO NAMED SCRATCH (wc_pseq, wc_pnick, ...)
; rather than carried in registers to the store: a private message has seven
; things to say about itself and an 8086 has four registers to say them in,
; and the first spelling of this file juggled them through the stack and lost
; the text on the way.
; =============================================================================

; -----------------------------------------------------------------------------
; wc_compose - write request [wc_rq] into wc_reqb, set [wc_reqn]
; in:  the UI task; [wc_rq] = RQ_JOIN / POLL / SAY / FLAG
; out: nothing; clobbers AX, BX, CX, DX, SI, DI
;
; The request line, then SPEC.md 113.3's headers, then the body. The body's
; length is known before a header is written - it is a NUL string already in
; our bss (the nick, or the queued message) - so Content-Length is written
; from it and the body copied after the blank line. Every append is bounded
; by BX, set once: WC_REQMAX is sized for the longest of each piece, and a
; bound reached is a request cut rather than one that overruns the bss.
; -----------------------------------------------------------------------------
wc_compose:
    ; --- the view this request speaks for -------------------------------------
    mov al, [wc_view]
    mov [wc_rqview], al
    mov al, [wc_vgen]
    mov [wc_rqvgen], al
    mov byte [wc_rqh], 0
    ; --- the request line ------------------------------------------------------
    mov di, wc_reqb
    mov bx, wc_reqb + WC_REQMAX - 1
    mov si, wc_s_post
    call wc_put
    mov si, wc_pfx
    call wc_put
    mov si, wc_s_v1
    call wc_put
    mov al, [wc_rq]
    mov si, wc_s_join
    cmp al, RQ_JOIN
    je .verb
    mov si, wc_s_poll
    cmp al, RQ_POLL
    je .verb
    mov si, wc_s_say
    cmp al, RQ_SAY
    je .verb
    mov si, wc_s_flag
.verb:
    call wc_put
    ; --- the query (SPEC.md 113.2.1) ---------------------------------------
    cmp byte [wc_rq], RQ_JOIN
    je .jq
    mov si, wc_q_t                      ; ?t=<token>
    call wc_put
    mov si, wc_tok
    call wc_put
    mov si, wc_q_v                      ; &v=<view>
    call wc_put
    call wc_putview
    mov si, wc_q_s                      ; &s=<seq>
    call wc_put
    mov ax, [wc_seq]
    mov dx, [wc_seq+2]
    call wc_putd32
    mov si, wc_q_r                      ; &r=<roster version>
    call wc_put
    mov ax, [wc_rver]
    mov dx, [wc_rver+2]
    call wc_putd32
    mov al, [wc_rq]
    cmp al, RQ_FLAG
    je .fq
    cmp al, RQ_POLL
    jne .qend
    call wc_putst                       ; poll: &st=<status>
    cmp byte [wc_hist], 0               ; ...and &h=1 when the view is new
    je .qend
    mov byte [wc_rqh], 1
    mov si, wc_q_h
    call wc_put
    jmp short .qend
.fq:
    mov si, wc_q_m                      ; flag: &m=<seq>
    call wc_put
    mov ax, [wc_qflag]
    mov dx, [wc_qflag+2]
    call wc_putd32
    jmp short .qend
.jq:
    mov si, wc_q_jv                     ; join: ?v=<view>&st=<status>
    call wc_put
    call wc_putview
    call wc_putst
    mov byte [wc_rqh], 1                ; a join always answers with history
.qend:
    ; --- the headers ----------------------------------------------------------
    mov si, wc_s_http                   ; ' HTTP/1.0' CRLF 'Host: '
    call wc_put
    mov si, wc_host
    call wc_put
    cmp word [wc_port], 80
    je .hp
    mov al, ':'
    call wc_putc
    mov ax, [wc_port]
    xor dx, dx
    call wc_putd32
.hp:
    mov si, wc_s_tail                   ; User-Agent, Connection, and the
    call wc_put                         ; length's name
    call wc_body                        ; SI = the body, CX = its length
    push si
    mov ax, cx
    xor dx, dx
    call wc_putd32
    mov si, wc_s_crlf2
    call wc_put
    pop si
    call wc_put                         ; ...and the body itself
    cmp byte [wc_rq], RQ_JOIN           ; a join's second line: the code
    jne .nkey
    cmp byte [wc_key], 0
    je .nkey
    mov al, 10
    call wc_putc
    mov si, wc_key
    call wc_put
.nkey:
    sub di, wc_reqb
    mov [wc_reqn], di
    ret

; --- wc_body - SI = this request's body, CX = its length. clobbers AL --------
; A join's body is the nick, and when there is a code, LF and the code
; (SPEC.md 113.2.2); SI is the nick alone, and wc_compose appends the rest.
wc_body:
    xor cx, cx
    mov si, wc_nick                     ; join: the nick
    mov al, [wc_rq]
    cmp al, RQ_JOIN
    jne .say
    cmp byte [wc_key], 0
    je .len
    mov si, wc_key
    call .len
    inc cx                              ; ...the LF
    mov si, wc_nick
    jmp short .len
.say:
    mov si, wc_qtext                    ; say: the message
    cmp al, RQ_SAY
    je .len
    mov si, wc_empty                    ; poll, flag: nothing
.len:                                   ; CX += the string's length
    push si
.c:
    lodsb
    or al, al
    jz .out
    inc cx
    jmp short .c
.out:
    pop si
    ret

; --- wc_putst - '&st=<status>', and the status is now the server's --------
wc_putst:
    mov si, wc_q_st
    call wc_put
    mov al, [wc_st]
    mov [wc_stsent], al
    add al, '0'
    jmp short wc_putc

; --- wc_putview - the view as the query spells it: a digit, or @nick --------
wc_putview:
    mov al, [wc_view]
    cmp al, WV_DM
    je .dm
    add al, '0'
    jmp short wc_putc
.dm:
    mov al, '@'
    call wc_putc
    mov si, wc_vdm
    jmp short wc_put

; -----------------------------------------------------------------------------
; wc_put / wc_putc / wc_putd32 - append to DS:DI, never past DS:BX
; wc_put: SI = a NUL string, left past it. wc_putc: AL. wc_putd32: DX:AX.
; out: DI advanced; every other register preserved, but wc_put's SI and
;      wc_putd32's AX and DX
; -----------------------------------------------------------------------------
wc_put:
    push ax
.c:
    lodsb
    or al, al
    jz .out
    call wc_putc
    jmp short .c
.out:
    pop ax
    ret

wc_putc:
    cmp di, bx
    ja .out
    mov [di], al
    inc di
.out:
    ret

wc_putd32:
    push si
    push bx
    push cx
    push di
    call wc_d32str                      ; SI = the digits
    pop di
    pop cx
    pop bx
    call wc_put
    pop si
    ret

; -----------------------------------------------------------------------------
; wc_d32str - DX:AX in decimal, as a NUL string in wc_dbuf
; out: SI = the first digit; clobbers AX, BX, CX, DX, DI
;
; An 8086 divides 32 bits by 16 only when the quotient fits 16, so the
; division is the long-hand two-step: the high word by ten, then its
; remainder and the low word by ten.
; -----------------------------------------------------------------------------
wc_d32str:
    mov di, wc_dbuf + 11
    mov byte [di], 0
    mov cx, 10
.div:
    push ax
    mov ax, dx
    xor dx, dx
    div cx                              ; AX = high / 10, DX = its remainder
    mov bx, ax
    pop ax
    div cx                              ; DX:AX / 10 -> AX, remainder DX
    add dl, '0'
    dec di
    mov [di], dl
    mov dx, bx                          ; DX:AX = the quotient
    mov si, ax
    or si, dx
    jnz .div
    mov si, di
    ret

; =============================================================================
; wc_reply - read the answer in wc_resp into the stores (SPEC.md 113.2.3)
; in:  the UI task; the lock NOT held
; out: [wc_ercode] = 0 for OK, the ER code, or 0xFFFF for a reply that is
;      neither; [wc_erwait] = its wait. clobbers everything but BP
; =============================================================================
wc_reply:
    mov byte [wc_rqnote], 0
    mov word [wc_ercode], 0xFFFF
    mov word [wc_erwait], 0
    mov si, wc_resp
    call wc_lineend                     ; DI = the next line; the LF a NUL
    push di
    cmp word [si], 'OK'
    je .ok
    cmp word [si], 'ER'
    jne .end
    ; --- ER <code> <wait> <text> ------------------------------------------
    add si, 3
    call wc_dec32
    mov [wc_ercode], ax
    call wc_dec32
    mov [wc_erwait], ax
    cmp byte [si], ' '
    jne .end
    inc si
    call wc_setnote                     ; the sentence, for the notice line
    jmp short .end
.ok:
    ; --- OK <seq> <hhmm> <wait> ------------------------------------------
    mov word [wc_ercode], 0
    add si, 3
    call wc_dec32
    mov [wc_seq], ax
    mov [wc_seq+2], dx
    call wc_dec32                       ; hhmm: the server's UTC now
    push si
    call wc_tzcalc
    pop si
    call wc_dec32
    mov [wc_erwait], ax
    ; The history this answer carries REPLACES the view's messages - but only
    ; when the answer is still for the view the user is looking at
    call wc_forview
    jne .recs
    cmp byte [wc_rqh], 0
    je .recs
    mov byte [wc_hist], 0
    call wc_mclear
.recs:
    pop si                              ; the next line
.line:
    cmp byte [si], 0
    je .out
    call wc_lineend
    push di
    mov al, [si]
    cmp byte [si+1], ' '
    jne .next
    add si, 2
    mov bx, wc_rtab
.find:
    cmp byte [bx], 0
    je .next
    cmp al, [bx]
    je .call
    add bx, 3
    jmp short .find
.call:
    call [bx+1]
.next:
    pop si
    jmp short .line
.end:
    pop si
.out:
    ret

; the record types, and who reads each (SPEC.md 113.2.3's table)
wc_rtab:
    db 'M'
    dw wc_rec_m
    db 'D'
    dw wc_rec_d
    db 'C'
    dw wc_rec_c
    db 'X'
    dw wc_rec_x
    db 'R'
    dw wc_rec_r
    db 'P'
    dw wc_rec_p
    db 'N'
    dw wc_setnote
    db 'J'
    dw wc_rec_j
    db 'L'
    dw wc_rec_l
    db 0

; --- wc_forview - ZF = 1 when the reply is for the view on the glass --------
wc_forview:
    push ax
    mov al, [wc_rqvgen]
    cmp al, [wc_vgen]
    pop ax
    ret

; --- wc_lineend - SI = a line: its LF becomes a NUL; DI = the next line ----
wc_lineend:
    mov di, si
.c:
    mov al, [di]
    or al, al
    jz .out
    inc di
    cmp al, 10
    jne .c
    mov byte [di-1], 0
.out:
    ret

; --- wc_fld - cut the next space-separated field off at SI -------------------
; out: BX = the field (NUL-terminated in place), SI = past it. clobbers AL
wc_fld:
    mov bx, si
.c:
    mov al, [si]
    or al, al
    jz .out
    inc si
    cmp al, ' '
    jne .c
    mov byte [si-1], 0
.out:
    ret

; --- wc_ncopy - the NUL string at BX to DI, at most CX-1 characters ---------
; out: DI's copy NUL-terminated; clobbers AL, BX, CX (DI is left on the NUL)
wc_ncopy:
    dec cx
.c:
    mov al, [bx]
    or al, al
    jz .end
    jcxz .end
    mov [di], al
    inc di
    inc bx
    dec cx
    jmp short .c
.end:
    mov byte [di], 0
    ret

; --- wc_setnote - the notice line is the NUL string at SI. Preserves all ----
wc_setnote:
    push ax
    push bx
    push cx
    push di
    mov byte [wc_rqnote], 1             ; for wc_ondone: the reply spoke
    mov bx, si
    mov di, wc_note
    mov cx, WC_NOTEN
    call wc_ncopy
    or word [wc_dirty], WCD_NOTE
    pop di
    pop cx
    pop bx
    pop ax
    ret

; --- J token nick --------------------------------------------------------------
wc_rec_j:
    call wc_fld
    mov di, wc_tok
    mov cx, 13
    call wc_ncopy
    mov bx, si
    mov di, wc_nick
    mov cx, WC_NICKN
    call wc_ncopy
    or word [wc_dirty], WCD_HDR
    ret

; --- L i name topic --------------------------------------------------------------
wc_rec_l:
    call wc_dec32
    cmp ax, WC_NROOM
    jae .out
    inc si                              ; the space
    mov cx, WC_ROOMN + WC_TOPICN
    mul cx
    add ax, wc_rooms
    mov di, ax
    push di
    call wc_fld
    mov cx, WC_ROOMN
    call wc_ncopy
    pop di
    add di, WC_ROOMN
    mov bx, si
    mov cx, WC_TOPICN
    call wc_ncopy
    or word [wc_dirty], WCD_LEFT | WCD_TITLE
.out:
    ret

; --- C c0 c1 c2 c3 -----------------------------------------------------------
; Being the view IS having read it: the view's counter is marked seen as it
; arrives, so a room never counts itself unread.
wc_rec_c:
    xor di, di
.c:
    push di
    call wc_dec32
    pop di
    mov [wc_cnt+di], ax
    inc di
    inc di
    cmp di, 2 * WC_NROOM
    jb .c
    call wc_markseen
    or word [wc_dirty], WCD_LEFT
    ret

wc_markseen:
    mov al, [wc_view]
    cmp al, WC_NROOM
    jae .out
    xor ah, ah
    shl ax, 1
    mov bx, ax
    mov ax, [wc_cnt+bx]
    mov [wc_seen+bx], ax
.out:
    ret

; --- R ver n: the roster follows, whole -----------------------------------
wc_rec_r:
    call wc_dec32
    mov [wc_rver], ax
    mov [wc_rver+2], dx
    mov byte [wc_rn], 0
    or word [wc_dirty], WCD_RIGHT | WCD_LEFT | WCD_STAT
    ret

; --- P nick room st -----------------------------------------------------------
wc_rec_p:
    mov al, [wc_rn]
    cmp al, WC_NROS
    jae .out
    xor ah, ah
    mov cl, 4                           ; WC_RREC = 16
    shl ax, cl
    add ax, wc_ros
    mov di, ax
    push di
    call wc_fld
    mov cx, WC_NICKN
    call wc_ncopy
    call wc_dec32
    pop di
    mov [di+WC_RROOM], al
    push di
    call wc_dec32
    pop di
    cmp al, ST_BUSY
    jbe .st
    mov al, ST_ONLINE
.st:
    mov [di+WC_RST], al
    inc byte [wc_rn]
.out:
    ret
%if WC_RREC != 16
  %error "wc_rec_p indexes the roster with a shift by 4"
%endif

; --- X seq: the message is gone ------------------------------------------------
wc_rec_x:
    call wc_dec32
    call wc_mfind                       ; CF = 0: BX = its index
    jc .out
    call wc_mdel
.out:
    ret

; -----------------------------------------------------------------------------
; M seq room hhmm nick text - a room message
; -----------------------------------------------------------------------------
wc_rec_m:
    call wc_seqmin                      ; seq, then room, then hhmm
    call wc_fld
    mov [wc_pnick], bx
    mov [wc_ptext], si
    call wc_forview                     ; for the view the request was for...
    jne .out
    mov al, [wc_proom]                  ; ...and the room that view is?
    cmp al, [wc_view]
    jne .out
    mov bx, [wc_pnick]
    call wc_mineflag                    ; [wc_pmine], and WCM_MINE in AL
    mov [wc_pflg], al
    call wc_madd
    jc .out
    cmp byte [wc_pmine], 0
    jne .out
    call wc_unseen                      ; the divider...
    call wc_mention                     ; ...and a chirp if it names us
.out:
    ret

; --- wc_seqmin - an M record's seq, room and hhmm into the scratch ------------
; A D record has no room, and wc_rec_d reads its two numbers itself.
wc_seqmin:
    call wc_dec32
    mov [wc_pseq], ax
    mov [wc_pseq+2], dx
    call wc_dec32
    mov [wc_proom], al
    call wc_dec32
    push si
    call wc_hhmm2min
    pop si
    mov [wc_pmin], ax
    inc si                              ; the space before the next field
    ret

; --- wc_mineflag - BX = the sender: [wc_pmine], and AL = WCM_MINE or 0 -------
wc_mineflag:
    xor al, al
    call wc_isme
    jne .out
    mov al, WCM_MINE
.out:
    mov [wc_pmine], al
    ret

; -----------------------------------------------------------------------------
; D seq hhmm from to text - a private message, to or from this session
;
; The conversation is the OTHER end: `to` when we sent it, `from` when we did
; not. Shown, it goes in the store with the sender's nick; not shown, it
; lights the conversation in the left pane - unless we sent it, from another
; window of the same session (a second machine on the same nick is how).
; -----------------------------------------------------------------------------
wc_rec_d:
    call wc_dec32
    mov [wc_pseq], ax
    mov [wc_pseq+2], dx
    call wc_dec32
    push si
    call wc_hhmm2min
    pop si
    mov [wc_pmin], ax
    inc si
    call wc_fld                         ; from
    mov [wc_pnick], bx
    call wc_fld                         ; to
    mov [wc_pother], bx
    mov [wc_ptext], si
    mov bx, [wc_pnick]
    call wc_mineflag
    or al, WCM_DM
    mov [wc_pflg], al
    cmp byte [wc_pmine], 0
    jne .conv
    mov bx, [wc_pnick]                  ; not ours: the other end is the sender
    mov [wc_pother], bx
.conv:
    mov bx, [wc_pother]
    call wc_dmconv                      ; DI = the conversation's record
    ; --- is the conversation the view? -------------------------------------
    cmp byte [wc_view], WV_DM
    jne .aside
    call wc_forview
    jne .aside
    mov si, wc_vdm
    push di
    mov di, [wc_pother]
    call wc_streqci
    pop di
    jne .aside
    call wc_madd
    jc .out
    cmp byte [wc_pmine], 0
    jne .out
    call wc_unseen
    jmp short wc_chirp
.aside:
    cmp byte [wc_pmine], 0
    jne .out
    cmp byte [di+WC_DUNR], 99
    jae .lit
    inc byte [di+WC_DUNR]
.lit:
    or word [wc_dirty], WCD_LEFT
    jmp short wc_chirp
.out:
    ret

; --- wc_chirp - a notification, when they are on and we are not in front ---
; 880 Hz for two ticks, Telnet's bell (teansi.inc), at a package's priority.
wc_chirp:
    cmp byte [wc_notify], 0
    je .out
    cmp byte [wc_front], 0
    jne .out
    push ax
    push cx
    push dx
    mov ax, 880
    mov cx, 2
    mov dl, 0x40
    call OSAPI_SND_TONE                 ; CF = 1 refused: a beep that does not
    pop dx                              ; happen, which is all it can be
    pop cx
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; wc_mention - the message just added names us, as a word: chirp
; A word is a run the nick's own characters do not continue, so "Al" is not
; mentioned by "Alan". Case-blind.
; -----------------------------------------------------------------------------
wc_mention:
    mov si, [wc_ptext]
.at:
    cmp byte [si], 0
    je .out
    mov di, wc_nick
    push si
.c:
    mov al, [di]
    or al, al
    jz .end
    mov ah, [si]
    xchg al, ah
    call wc_lower
    xchg al, ah
    call wc_lower
    cmp al, ah
    jne .miss
    inc si
    inc di
    jmp short .c
.end:
    mov al, [si]                        ; the character after: not one a nick
    call wc_nickch                      ; could go on with
    pop si
    jnc .next
    cmp si, [wc_ptext]                  ; ...and the one before
    je wc_chirp
    mov al, [si-1]
    call wc_nickch
    jc wc_chirp
    jmp short .next
.miss:
    pop si
.next:
    inc si
    jmp short .at
.out:
    ret

; --- wc_nickch - CF = 0 when AL could be in a nick ([A-Za-z0-9_-]) ---------
wc_nickch:
    push ax
    call wc_lower
    cmp al, 'a'
    jb .dig
    cmp al, 'z'
    jbe .yes
.dig:
    cmp al, '0'
    jb .sym
    cmp al, '9'
    jbe .yes
.sym:
    cmp al, '_'
    je .yes
    cmp al, '-'
    je .yes
    pop ax
    stc
    ret
.yes:
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; wc_unseen - a message from someone else was just added, at the end
; While the window is not in front it starts, or grows, the divider above the
; first of them (SPEC.md 113.6.5). The count is in the divider's own words,
; so the pane is drawn again rather than only its tail.
; -----------------------------------------------------------------------------
wc_unseen:
    cmp byte [wc_front], 0
    jne .out
    cmp byte [wc_divi], 0xFF
    jne .more
    mov al, [wc_mn]
    dec al
    mov [wc_divi], al
.more:
    inc byte [wc_divn]
    or word [wc_dirty], WCD_PANE
.out:
    ret

; -----------------------------------------------------------------------------
; wc_dmconv - the conversation with the nick at BX, made if it is new
; out: DI = its record; preserves BX. clobbers AX, CX, SI
;
; A new conversation goes at the TOP: the list is most recent first, so a
; full list loses its oldest, at the bottom.
; -----------------------------------------------------------------------------
wc_dmconv:
    xor cx, cx
    mov cl, [wc_dn]
    mov di, wc_dms
    jcxz .new
.f:
    mov si, di
    push di
    mov di, bx
    call wc_streqci
    pop di
    je .out
    add di, WC_DREC
    loop .f
.new:
    mov al, [wc_dn]                     ; how many move down one: all of them,
    cmp al, WC_NDM                      ; or all but the oldest when full
    jb .move
    dec al
.move:
    xor ah, ah
    mov cl, 4                           ; WC_DREC = 16
    shl ax, cl
    mov cx, ax
    jcxz .put
    mov si, wc_dms - 1                  ; the last byte of what moves...
    add si, cx
    mov di, si                          ; ...to one record further on,
    add di, WC_DREC                     ; backwards so it cannot overlap
    push es
    push ds
    pop es
    std
    rep movsb
    cld
    pop es
.put:
    cmp byte [wc_dn], WC_NDM
    jae .full
    inc byte [wc_dn]
.full:
    mov di, wc_dms
    push bx
    mov cx, WC_NICKN
    call wc_ncopy
    pop bx
    mov di, wc_dms
    mov byte [di+WC_DUNR], 0
    or word [wc_dirty], WCD_LEFT
.out:
    ret
%if WC_DREC != 16
  %error "wc_dmconv indexes the conversations with a shift by 4"
%endif

; -----------------------------------------------------------------------------
; wc_isme - ZF = 1 when the nick at BX is ours (case-blind). Preserves all.
; wc_streqci - ZF = 1 when the NUL strings at SI and DI are equal, case-blind.
;              Preserves all.
; -----------------------------------------------------------------------------
wc_isme:
    push si
    push di
    mov si, wc_nick
    mov di, bx
    call wc_streqci
    pop di
    pop si
    ret

wc_streqci:
    push ax
    push si
    push di
.c:
    mov al, [si]
    call wc_lower
    mov ah, al
    mov al, [di]
    call wc_lower
    cmp al, ah
    jne .out
    inc si
    inc di
    or al, al
    jnz .c
                                        ; both ended together: ZF = 1 from the
.out:                                   ; `or`
    pop di
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; wc_hhmm2min - AX = hhmm (1432) -> AX = minutes since midnight (872)
; clobbers CX, DX
; -----------------------------------------------------------------------------
wc_hhmm2min:
    xor dx, dx
    mov cx, 100
    div cx                              ; AX = hh, DX = mm
    push dx
    mov cx, 60
    mul cx
    pop dx
    add ax, dx
    ret

; -----------------------------------------------------------------------------
; wc_tzcalc - the server's UTC hhmm is in AX: the offset to the local clock
; out: [wc_tzo] = local - UTC in minutes, rounded to 15, in -720..+720;
;      [wc_h12] = the clock's 12-hour choice. clobbers AX, BX, CX, DX
; -----------------------------------------------------------------------------
wc_tzcalc:
    call wc_hhmm2min
    push ax
    call OSAPI_CLK_GET                  ; BH:BL = the local hour and minute
    shr al, 1                           ; AL bit 1: 12-hour display
    and al, 1
    mov [wc_h12], al
    mov al, bh
    mov cl, 60
    mul cl
    xor bh, bh
    add ax, bx                          ; AX = the local minute
    pop dx
    sub ax, dx                          ; local - UTC, -1439..1439
    cmp ax, 720
    jle .lo
    sub ax, 1440
.lo:
    cmp ax, -720
    jge .round
    add ax, 1440
.round:
    add ax, 7 + 1440                    ; to the nearest 15: shift positive,
    xor dx, dx                          ; round down, shift back
    mov cx, 15
    div cx
    mul cx
    sub ax, 1440
    mov [wc_tzo], ax
    ret

; =============================================================================
; THE MESSAGE STORE (SPEC.md 113.10): WC_NMSG records of WC_MREC, in sequence
; order, index 0 the oldest.
; =============================================================================

; --- wc_mrec - BX = an index -> BX = its record. Preserves the rest. -------
wc_mrec:
    push ax
    push dx
    mov ax, WC_MREC
    mul bx
    add ax, wc_msgs
    mov bx, ax
    pop dx
    pop ax
    ret

; --- wc_mclear - no messages, nothing selected, no divider --------------------
wc_mclear:
    mov byte [wc_mn], 0
    mov al, 0xFF
    mov [wc_sel], al
    mov [wc_divi], al
    mov [wc_find], al
    mov byte [wc_divn], 0
    mov word [wc_top], 0
    mov word [wc_total], 0
    mov byte [wc_tobot], 1              ; what fills it next is read from the
    or word [wc_dirty], WCD_PANE        ; NEWEST end, as every chat is
    ret

; -----------------------------------------------------------------------------
; wc_madd - append the message in the parser's scratch
; in:  [wc_pseq], [wc_pmin], [wc_pflg], [wc_pnick], [wc_ptext]
; out: CF = 0 added; CF = 1 refused because it is not newer than the newest
;      held - a history overlapping what arrived since (SPEC.md 113.2.3).
;      clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
wc_madd:
    mov ax, [wc_pseq]
    mov dx, [wc_pseq+2]
    mov bl, [wc_mn]
    xor bh, bh
    or bx, bx
    jz .fresh
    dec bx
    call wc_mrec
    cmp dx, [bx+WC_MSEQ+2]
    ja .fresh
    jb .no
    cmp ax, [bx+WC_MSEQ]
    ja .fresh
.no:
    stc
    ret
.fresh:
    cmp byte [wc_mn], WC_NMSG
    jb .room
    xor bx, bx
    call wc_mdel                        ; full: the oldest goes
.room:
    mov bl, [wc_mn]
    xor bh, bh
    call wc_mrec
    mov ax, [wc_pseq]
    mov [bx+WC_MSEQ], ax
    mov ax, [wc_pseq+2]
    mov [bx+WC_MSEQ+2], ax
    mov ax, [wc_pmin]
    mov [bx+WC_MMIN], ax
    mov al, [wc_pflg]
    mov [bx+WC_MFLG], al
    mov byte [bx+WC_MLINES], 0          ; wrapped when it is drawn
    lea di, [bx+WC_MTEXT]
    push bx
    mov bx, [wc_ptext]
    mov cx, WC_TEXTN
    call wc_ncopy
    pop bx
    lea di, [bx+WC_MNICK]
    mov bx, [wc_pnick]
    mov cx, WC_NICKN
    call wc_ncopy
    inc byte [wc_mn]
    or word [wc_dirty], WCD_TAIL
    clc
    ret

; -----------------------------------------------------------------------------
; wc_mfind - DX:AX = a seq -> CF = 0 and BX = the index holding it, or CF = 1
; clobbers CX
; -----------------------------------------------------------------------------
wc_mfind:
    xor cx, cx
    mov cl, [wc_mn]
    jcxz .no
    mov bx, wc_msgs
.c:
    cmp ax, [bx+WC_MSEQ]
    jne .n
    cmp dx, [bx+WC_MSEQ+2]
    je .yes
.n:
    add bx, WC_MREC
    loop .c
.no:
    stc
    ret
.yes:
    mov ax, bx                          ; the record back to its index
    sub ax, wc_msgs
    xor dx, dx
    mov cx, WC_MREC
    div cx
    mov bx, ax
    clc
    ret

; -----------------------------------------------------------------------------
; wc_mdel - remove message BX (an index)
; out: clobbers AX, CX, SI, DI. The selection, the divider and the find hit
;      that pointed past it move down with the records they name; one that
;      pointed AT it is gone.
; -----------------------------------------------------------------------------
wc_mdel:
    push bx
    push dx
    mov al, [wc_mn]
    xor ah, ah
    sub ax, bx
    dec ax                              ; records after it
    mov cx, WC_MREC
    mul cx
    mov cx, ax
    call wc_mrec                        ; BX = its record
    mov di, bx
    lea si, [bx+WC_MREC]
    push es
    push ds
    pop es
    rep movsb
    pop es
    pop dx
    pop bx
    dec byte [wc_mn]
    mov si, wc_sel
    call wc_idxfix
    mov si, wc_find
    call wc_idxfix
    mov al, [wc_divi]                   ; the divider is ABOVE a message, so
    cmp al, 0xFF                        ; losing its message moves it onto the
    je .done                            ; next one - unless there is none
    cmp al, bl
    jb .done
    je .on
    dec byte [wc_divi]
    jmp short .done
.on:
    cmp al, [wc_mn]
    jb .done
    mov byte [wc_divi], 0xFF
.done:
    or word [wc_dirty], WCD_PANE
    ret

; --- wc_idxfix - the index byte at SI, after message BL went ------------------
wc_idxfix:
    mov al, [si]
    cmp al, 0xFF
    je .out
    cmp al, bl
    jb .out
    ja .down
    mov byte [si], 0xFF
    ret
.down:
    dec byte [si]
.out:
    ret
