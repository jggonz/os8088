; =============================================================================
; os8088 - tests/radgate/radgate.asm
;
; RADGATE: the RAD replayer's gate client (SPEC.md 96.8). It drives
; OSAPI_SND_FM verbs 3-6 before RADBOX exists, and keeps every answer in its
; own image where tests/radopl3.py, radopl2.py and radrtc.py read it back
; through the window's W_SEG - it never ships.
;
; Keys:
;   1 / 2 / 3 / 4   load + start RV1.RAD / RV2.RAD / RV1SLOW.RAD / RV2BPM.RAD
;   5 / 6 / 7       load + start FAN.RAD / FANALL.RAD / HEAVY.RAD (the
;                   fan-out tunes: the first two HALT, SPEC.md 96.4.5)
;   l / n           load RV1.RAD / FAN.RAD only (no start)
;   g / p / r / s   verb 5 start (a restart while playing) / pause / resume /
;                   stop
;   x               verb 3 all-off (unloads the tune)
;   v               feed tests/unit/radrows.py's hostile-file table to verb 4,
;                   row by row, and keep every answer (rt_vres)
;   a               the register-contract rows (rt_ares): refusals that must
;                   answer AH = 0 whatever AH went in, and keep CX
;   w               int 15h AH=86h, a 10 s BIOS wait, from THIS task: the
;                   RTC pacer's chain path (SPEC.md 34.13.3 steps 1-2) while a
;                   tune plays - the ticks either side kept in rt_wt0/rt_wt1
;   c / f / k       tests/radmove.py's arena (SPEC.md 34.12.8): claim
;                   OSAPI_MEM_CLAIM_HI of [rt_mkb] KB into slot [rt_mslot] /
;                   free that slot / claim it and fill every whole 4KB of it
;                   with FAh F4h (cli, hlt) - the forcing ask, whose block is
;                   where a moved image WAS, so a stale far jump into it stops
;                   the machine instead of running whatever is there. The
;                   harness pokes rt_mkb and rt_mslot; rt_mres is CF and DX
;
; THE POLL DUTY (SPEC.md 34.12.2): a WM_TIMER every 2 ticks calls verb 6 into
; rt_stat for as long as the window lives, loaded or not - this client is a
; tune owner like any other and heals SPEC.md 34.13.7's stale 0 the same way.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'RADGATE', rt_entry

RT_BUFKB    equ 49              ; the file buffer: RAD_MAXLEN + 1 byte fits

rt_entry:
    push ax
    push si
    mov ax, RT_BUFKB
    call OSAPI_MEM_CLAIM        ; DX = the buffer
    jc .fail
    mov [rt_buf], dx
    mov si, rt_tpl
    call OSAPI_WM_CREATE        ; BX = window ptr
    jc .fail
    mov ax, rt_poll
    call OSAPI_WM_ONTIMER
    mov ax, 2
    call OSAPI_WM_TIMER
    clc
.fail:
    pop si
    pop ax
    ret

rt_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, si
    call OSAPI_WM_CONTENT
    mov cx, ax
    add cx, 4
    add dx, 8
    mov si, rt_s1
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

rt_onclick:
    ret

; -----------------------------------------------------------------------------
; rt_poll - verb 6 every 2 ticks (the poll duty)
; -----------------------------------------------------------------------------
rt_poll:
    push ax
    push bx
    push cx
    push di
    cmp byte [rt_nopoll], 0     ; tests/radmove.py holds the poll off while
    jne .rearm                  ; SOUND.DRV is unmounted: a kernel defect on
                                ; main (drv_svc_call_x returns through
                                ; drv_svc_none's NEAR ret) hangs an FM verb
                                ; made with no sink. SPEC.md 96.8 - decision
                                ; D8: the fix is a PR of its own against main,
                                ; not RADBOX's; a real package guards itself
                                ; with OSAPI_SND_CAPS instead (SPEC.md 96.2)
    mov al, SNDFM_RADSTAT
    mov bx, ds
    mov di, rt_stat
    mov cx, RAD_ST_LEN
    call OSAPI_SND_FM
    mov [rt_pollax], ax
    mov byte [rt_statok], 0
    jc .rearm
    mov byte [rt_statok], 1
.rearm:
    inc word [rt_polls]
    mov bx, si
    mov ax, 2
    call OSAPI_WM_TIMER
    pop di
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; rt_onkey
; -----------------------------------------------------------------------------
rt_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    cmp al, '1'
    jb .notnum
    cmp al, '7'
    ja .notnum
    sub al, '1'
    xor ah, ah
    shl ax, 1
    mov bx, ax
    mov si, [rt_names+bx]
    call rt_loadfile
    jc .show
    mov ah, RADP_START
    call rt_play
    jmp .show
.notnum:
    cmp al, 'l'
    jne .notl
    mov si, rt_n1
    call rt_loadfile
    jmp .show
.notl:
    cmp al, 'n'
    jne .notn
    mov si, rt_n5               ; FAN.RAD loaded, not started (radmove.py)
    call rt_loadfile
    jmp .show
.notn:
    mov ah, RADP_START
    cmp al, 'g'
    je .play
    mov ah, RADP_PAUSE
    cmp al, 'p'
    je .play
    mov ah, RADP_RESUME
    cmp al, 'r'
    je .play
    mov ah, RADP_STOP
    cmp al, 's'
    je .play
    cmp al, 'x'
    jne .notx
    mov al, 3
    call OSAPI_SND_FM
    call rt_keep
    jmp .show
.notx:
    cmp al, 'v'
    jne .nota
    call rt_table
    jmp .show
.nota:
    cmp al, 'a'
    jne .notw
    call rt_ahrows
    jmp .show
.notw:
    cmp al, 'w'
    jne .notc
    call rt_wait
    jmp .out
.notc:
    cmp al, 'c'
    jne .notf
    xor al, al
    call rt_mem
    jmp .out
.notf:
    cmp al, 'f'
    jne .notk
    mov al, 2
    call rt_mem
    jmp .out
.notk:
    cmp al, 'k'
    jne .out
    mov al, 1
    call rt_mem
    jmp .out
.play:
    call rt_play
.show:
    call rt_text
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov bx, si
    call OSAPI_WM_CONTENT
    mov bx, dx
    mov cx, ax
    add cx, 180
    add dx, 20
    call OSAPI_GFX_FILL
    call rt_paint
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

rt_play:
    mov al, SNDFM_RADPLAY
    call OSAPI_SND_FM
    call rt_keep
    ret

; rt_keep - the last verb's answer into rt_res: +0 CF, +1 AL, +2 AH, +3 CX
rt_keep:
    pushf
    push ax
    mov byte [rt_res], 0
    jnc .ok
    mov byte [rt_res], 1
.ok:
    mov [rt_res+1], ax
    mov [rt_res+3], cx
    inc byte [rt_res+5]
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; rt_loadfile - SI = a name in the current directory: read it, verb 4
; out: CF as verb 4's (or the read's), rt_res
; -----------------------------------------------------------------------------
rt_loadfile:
    push bx
    push cx
    push dx
    push es
    mov es, [rt_buf]
    xor bx, bx
    mov cx, 0xC001
    xor dx, dx
    call OSAPI_FILE_READ        ; DX:AX = the size
    jc .readfail
    mov cx, ax
    mov al, SNDFM_RADLOAD
    mov bx, [rt_buf]
    push si
    xor si, si
    call OSAPI_SND_FM
    pop si
    call rt_keep
    jmp short .out
.readfail:
    mov byte [rt_res], 2
    stc
.out:
    pop es
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; rt_table - every row of build/radrows.inc through verb 4
; -----------------------------------------------------------------------------
rt_table:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cld
    mov si, rt_rows
    mov di, rt_vres
    mov word [rt_vn], 0
.row:
    lodsw                       ; the file's length
    cmp ax, 0xFFFF
    je .done
    mov dx, ax
    lodsw                       ; the prefix that is not zeros
    mov cx, ax
    push di
    mov es, [rt_buf]
    xor di, di
    rep movsb
    mov cx, dx
    sub cx, ax
    xor al, al
    rep stosb
    pop di
    push si
    push di
    mov ax, 0xA500 | SNDFM_RADLOAD  ; AH a stranger's byte: a refusal must
    mov bx, [rt_buf]                ; answer 0 there unless RADE_CORRUPT
    xor si, si
    mov cx, dx
    call OSAPI_SND_FM
    pop di
    pop si
    push ds
    pop es
    mov byte [di], 0
    jnc .keep
    mov byte [di], 1
.keep:
    mov [di+1], ax
    mov [di+3], cx
    add di, 5
    inc word [rt_vn]
    jmp .row
.done:
    mov al, 3                   ; unload whatever the table left loaded
    call OSAPI_SND_FM
    mov byte [rt_vdone], 1
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; rt_ahrows - SPEC.md 34.12's register contract on refusal paths that the
; hostile-file table cannot reach: every answer kept in rt_ares, 5 bytes a row
; (CF, AL, AH, CX), in this order -
;   0  verb 5, AH = 9 (an undefined sub-op), RV1.RAD loaded  -> BADARG, AH 0
;   1  verb 6, DI = FFF0h, CX = 100h, tune loaded            -> BADARG, AH 0,
;                                                               CX 100h
;   2  verb 3 all-off (unloads it), then verb 5 AH = RADP_STOP -> NOTUNE, AH 0
;   3  verb 4, SI = FF00h, CX = 0200h, AH = A5h                -> BADARG, AH 0
;                                                  (SI + CX = 10100h: high 01h)
;   4  verb 6, DI = 0, CX = 100h, AH = A5h, no tune          -> NOTUNE, AH 0,
;                                                               CX 100h
;   5  verb 7 (undefined), AH = 77h                           -> BADARG, AH 0
; -----------------------------------------------------------------------------
rt_ahrows:
    push ax
    push bx
    push cx
    push si
    push di
    mov di, rt_ares
    mov si, rt_n1
    call rt_loadfile
    mov ax, 0x0900 | SNDFM_RADPLAY
    call rt_akeep
    mov ax, 0xA500 | SNDFM_RADSTAT
    mov bx, ds
    push di
    mov di, 0xFFF0
    mov cx, 0x100
    call OSAPI_SND_FM
    pop di
    call rt_ares1
    mov al, 3
    call OSAPI_SND_FM
    mov ax, (RADP_STOP << 8) | SNDFM_RADPLAY
    call rt_akeep
    mov ax, 0xA500 | SNDFM_RADLOAD
    mov bx, [rt_buf]
    mov cx, 0x0200
    push si
    mov si, 0xFF00
    call OSAPI_SND_FM
    pop si
    call rt_ares1
    mov ax, 0xA500 | SNDFM_RADSTAT
    mov bx, ds
    push di
    xor di, di
    mov cx, 0x100
    call OSAPI_SND_FM
    pop di
    call rt_ares1
    mov ax, 0x7707
    call rt_akeep
    mov byte [rt_adone], 1
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; rt_wait - int 15h AH=86h for 10,000,000 us (CX:DX = 0098h:96C0h). SeaBIOS
; sets 40:A0 bit 0 and PIE and counts the wait on IRQ8, so every periodic
; interrupt of the tune's takes the pacer's chain path until it expires
; -----------------------------------------------------------------------------
rt_wait:
    push ax
    push cx
    push dx
    call OSAPI_GET_TICKS
    mov [rt_wt0], ax
    mov ah, 0x86
    mov cx, 0x0098
    mov dx, 0x96C0
    int 0x15
    mov byte [rt_wcf], 0
    jnc .ok
    mov byte [rt_wcf], 1
.ok:
    mov [rt_wah], ah
    call OSAPI_GET_TICKS
    mov [rt_wt1], ax
    inc byte [rt_wn]
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; rt_mem - tests/radmove.py's arena. AL = 0 claim HI, 1 claim HI + poison,
; 2 free; [rt_mkb] KB, slot [rt_mslot] (0..7); answer in rt_mres (CF, DX)
; -----------------------------------------------------------------------------
rt_mem:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    mov bl, [rt_mslot]
    and bx, 7
    shl bx, 1
    cmp al, 2
    jne .claim
    mov dx, [rt_mseg+bx]
    call OSAPI_MEM_FREE
    mov word [rt_mseg+bx], 0
    jmp short .keep
.claim:
    push ax
    mov ax, [rt_mkb]
    call OSAPI_MEM_CLAIM_HI
    pop ax
    jc .keep
    mov [rt_mseg+bx], dx
    cmp al, 1
    jne .ok
    mov es, dx
    mov bx, [rt_mkb]
    shr bx, 1
    shr bx, 1                   ; BX = whole 4KB chunks
    cld
.chunk:
    or bx, bx
    jz .ok
    xor di, di
    mov cx, 2048
    mov ax, 0xF4FA              ; FAh F4h: cli, hlt
    rep stosw
    mov ax, es
    add ax, 64
    mov es, ax
    dec bx
    jmp .chunk
.ok:
    clc
.keep:
    mov byte [rt_mres], 0
    jnc .k
    mov byte [rt_mres], 1
.k:
    mov [rt_mres+1], dx
    inc byte [rt_mres+3]
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

rt_akeep:                       ; AX = the verb: call it with CX = 5A5Ah
    mov cx, 0x5A5A
    call OSAPI_SND_FM
    ; fall through
rt_ares1:                       ; the answer (CF, AX, CX) into [DI], DI += 5
    mov byte [di], 0
    jnc .k
    mov byte [di], 1
.k:
    mov [di+1], ax
    mov [di+3], cx
    add di, 5
    ret

; rt_text - "cf al ah" of the last answer into rt_s1, for a screenshot
rt_text:
    push ax
    push bx
    mov al, [rt_res]
    add al, '0'
    mov [rt_s1+2], al
    mov al, [rt_res+1]
    mov bx, rt_s1+5
    call rt_hex
    mov al, [rt_res+2]
    mov bx, rt_s1+8
    call rt_hex
    pop bx
    pop ax
    ret

rt_hex:
    push ax
    push cx
    mov ah, al
    mov cl, 4
    shr al, cl
    call .dig
    mov [bx], al
    mov al, ah
    and al, 0x0F
    call .dig
    mov [bx+1], al
    pop cx
    pop ax
    ret
.dig:
    add al, '0'
    cmp al, '9'
    jbe .d
    add al, 7
.d:
    ret

rt_tpl:
    dw 40, 60, 190, 50
    dw rt_ttl, rt_paint, rt_onkey, rt_onclick

rt_ttl:     db 'RAD Gate', 0
rt_s1:      db 'c:- a:-- h:--', 0
rt_names:   dw rt_n1, rt_n2, rt_n3, rt_n4, rt_n5, rt_n6, rt_n7
rt_n1:      db 'RV1.RAD', 0
rt_n2:      db 'RV2.RAD', 0
rt_n3:      db 'RV1SLOW.RAD', 0
rt_n4:      db 'RV2BPM.RAD', 0
rt_n5:      db 'FAN.RAD', 0
rt_n6:      db 'FANALL.RAD', 0
rt_n7:      db 'HEAVY.RAD', 0

; --- what the gates read -----------------------------------------------------
rt_buf:     dw 0
rt_res:     db 0, 0, 0, 0, 0, 0 ; CF, AL, AH, CX, and a count of answers
rt_statok:  db 0                ; 1 = the last poll's verb 6 answered CF = 0
rt_polls:   dw 0
rt_pollax:  dw 0                ; the last poll's AX (AL = RADE_* on CF = 1)
rt_stat:    times RAD_ST_LEN db 0
rt_vdone:   db 0
rt_vn:      dw 0
rt_vres:    times 5 * 200 db 0  ; per row: CF, AL, AH, CX
rt_adone:   db 0
rt_ares:    times 5 * 6 db 0    ; rt_ahrows' six answers
rt_wn:      db 0                ; rt_wait: calls completed
rt_wcf:     db 0                ; ...the last one's CF and AH
rt_wah:     db 0
rt_wt0:     dw 0                ; ...and OSAPI_GET_TICKS either side
rt_wt1:     dw 0
rt_nopoll:  db 0                ; 1 = rt_poll makes no verb 6 (radmove.py,
                                ; SPEC.md 96.8's kernel defect; D8)
rt_mkb:     dw 0                ; rt_mem: KB (poked by tests/radmove.py)...
rt_mslot:   db 0                ; ...the slot...
rt_mseg:    times 8 dw 0        ; ...the segments held...
rt_mres:    db 0, 0, 0, 0       ; ...and the last answer: CF, DX, a count

rt_rows:
%include "radrows.inc"          ; generated by tests/radgate/mkrows.py
    dw 0xFFFF

    OS88_IMAGE_END
