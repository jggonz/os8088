; =============================================================================
; os8088 - apps/audio/apui.inc
;
; The Audio Player's window: layout computed from the LIVE content geometry
; every paint (SPEC.md 39 - three adapters, one binary), drawing kept
; deliberately plain (SPEC.md 86.7: no visualiser, no animation). Prefix apu_.
;
;   filename line
;   status line
;   MM:SS  [========------------]  MM:SS
;   [ Prev ][ Play ][Pause][ Stop ][ Next ][ Shuf ][ Rep ]
;   playlist:  > 02  SONG.WAV
; =============================================================================

    APBUF apu_t1, 8                 ; "MM:SS" scratch (elapsed)
    APBUF apu_t2, 8                 ; "MM:SS" scratch (total)
    APW   apu_ox                   ; cached content origin
    APW   apu_oy
apu_l_noname: db '(no file)', 0
apu_l_plhdr:  db 'Playlist:', 0

; =============================================================================
; apu_layout - recompute the button rects and the progress-bar rect from the
;              window's current content size.
; =============================================================================
apu_layout:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, [ap_win]
    call OSAPI_WM_GEOM             ; CX = content w, DX = content h, CF=1 hidden
    jc .keep                      ; not laid out yet: keep the last good values
    or cx, cx
    jz .keep
    mov [apu_cw], cx
    mov [apu_ch], dx
    jmp .have
.keep:
    mov cx, [apu_cw]
    or cx, cx
    jnz .have
    mov cx, 292                    ; a sane default until the real geometry lands
    mov [apu_cw], cx
    mov word [apu_ch], 210
.have:

    ; progress bar: x 46 .. w-46, y 31 .. 39
    mov word [apu_pbx1], 46
    mov ax, cx
    sub ax, 46
    mov [apu_pbx2], ax
    mov word [apu_pby1], 31
    mov word [apu_pby2], 39

    ; 7 buttons across x 4 .. w-4 on row y 46..62
    mov ax, cx
    sub ax, 8                      ; usable width
    mov bx, 7
    xor dx, dx
    div bx                         ; AX = per-button pitch
    mov [apu_bpitch], ax
    xor di, di                     ; button index
    xor si, si                     ; running x = 4
    mov si, 4
.brow:
    cmp di, 7
    jae .bdone
    mov bx, di
    mov cl, 3
    shl bx, cl                     ; * 8 bytes per rect
    add bx, apu_rects
    mov [bx+0], si                 ; x1
    mov word [bx+2], 46            ; y1
    mov ax, si
    add ax, [apu_bpitch]
    sub ax, 3                      ; small gap
    mov [bx+4], ax                 ; x2
    mov word [bx+6], 62            ; y2
    add si, [apu_bpitch]
    inc di
    jmp .brow
.bdone:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; apu_origin - AX = content left, DX = content top (screen coords)
; preserves BX, CX, SI, DI
; =============================================================================
apu_origin:
    push ax
    push bx
    push dx
    mov bx, [ap_win]
    call OSAPI_WM_CONTENT          ; out AX = left, DX = top (CF=1 not visible)
    jc .keep
    mov [apu_ox], ax
    mov [apu_oy], dx
.keep:
    pop dx
    pop bx
    pop ax
    ret

; =============================================================================
; apu_time_str - AX = seconds -> "MM:SS" NUL at DI (this segment). MM clamps
;                at 99.  preserves everything.
; =============================================================================
apu_time_str:
    push ax
    push bx
    push dx
    push di
    xor dx, dx
    mov bx, 60
    div bx                         ; AX = minutes, DX = seconds
    cmp ax, 99
    jbe .mok
    mov ax, 99
.mok:
    ; minutes -> two digits
    push dx
    xor dx, dx
    mov bx, 10
    div bx                         ; AX = tens, DX = ones
    add al, '0'
    mov [di], al
    mov ax, dx
    add al, '0'
    mov [di+1], al
    mov byte [di+2], ':'
    pop dx                         ; seconds
    mov ax, dx
    xor dx, dx
    mov bx, 10
    div bx
    add al, '0'
    mov [di+3], al
    mov ax, dx
    add al, '0'
    mov [di+4], al
    mov byte [di+5], 0
    pop di
    pop dx
    pop bx
    pop ax
    ret

; =============================================================================
; apu_progress -> AX = filled pixels of the progress bar (0 .. bar width)
; =============================================================================
apu_progress:
    push bx
    push cx
    push dx
    mov cx, [apu_pbx2]
    sub cx, [apu_pbx1]            ; CX = bar width in px
    dec cx
    or cx, cx
    jle .zero
    mov ax, [ap_total_pcm]
    mov dx, [ap_total_pcm+2]
    mov bx, ax
    or bx, dx
    jz .zero                      ; unknown length
    push si
    push di
    mov si, [ap_played]
    mov di, [ap_played+2]
.scale:
    cmp dx, cx                    ; DX:AX / CX must fit AX, or the 8086
    jb .fits                      ; faults (int 0): a track past ~14 MB, or
    shr dx, 1                     ; a hostile size. Halve total and played
    rcr ax, 1                     ; together - the ratio is what the bar shows
    shr di, 1
    rcr si, 1
    jmp .scale
.fits:
    ; step = total / barw   (>= 1 for any real track)
    div cx                        ; AX = total / barw  (DX:AX / cx)
    mov bx, ax
    xchg ax, si                   ; DX:AX = played (scaled), SI = step
    mov dx, di
    pop di
    pop si
    or bx, bx
    jz .full
    cmp dx, bx
    jae .full
    div bx                        ; AX = played / step  ~= filled px
    cmp ax, cx
    jbe .done
.full:
    mov ax, cx
    jmp .done
.zero:
    xor ax, ax
.done:
    pop dx
    pop cx
    pop bx
    ret

; =============================================================================
; apu_draw - full content repaint (kernel has white-filled the content first)
; =============================================================================
apu_draw:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call apu_layout
    call apu_origin

    cmp byte [ap_diag], 0
    je .normal
    call apu_draw_stats
    jmp .done
.normal:

    ; --- filename line -------------------------------------------------
    call apl_cur_name             ; SI = &name, CF = 1 empty
    jnc .havename
    mov si, apu_l_noname
.havename:
    mov cx, [apu_ox]
    add cx, 6
    mov dx, [apu_oy]
    add dx, 4
    mov ax, 0x0F00                 ; FONT_RUN: ink 0 on paper 15 (SPEC.md 6.6)
    call OSAPI_FONT_RUN

    ; --- status line -------------------------------------------------
    mov si, [ap_msg]
    mov cx, [apu_ox]
    add cx, 6
    mov dx, [apu_oy]
    add dx, 16
    mov ax, 0x0F00                 ; FONT_RUN: ink 0 on paper 15 (SPEC.md 6.6)
    call OSAPI_FONT_RUN

    ; --- time + bar -------------------------------------------------
    call apu_draw_time_full

    ; --- transport buttons ---------------------------------------
    call apu_draw_buttons

    ; --- playlist ------------------------------------------------
    call apu_draw_list

    ; --- the About card, on top of everything ------------------
    cmp byte [ap_abon], 0
    je .done
    call apu_about_card
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; apu_about_card - the OSAPI_ABOUT_SET card (SPEC.md 86.7): a white panel with
; a black frame and three lines, centred in the content box, on top of the
; player. Any click or key takes it down (ap_onclick / ap_onkey). Drawn under
; the gfx lock from apu_draw, so it survives a repaint and needs no flag of the
; window manager's.
; =============================================================================
apu_ab1: db 'Audio Player for os8088', 0
apu_ab2: db 'Version 0.6b', 0
apu_ab3: db 'Play PCM and ADPCM audio files', 0
apu_ab4: db 'Rates 8-22 kHz  +24/32/44 SB Pro', 0

apu_about_card:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    ; rect: inset 8 px each side of the content, 66 px tall, vertically centred
    mov ax, [apu_cw]
    cmp ax, 40
    jb .out                        ; content absurdly small - skip
    mov ax, [apu_ox]
    add ax, 8
    mov [apab_x1], ax              ; x1
    mov ax, [apu_ch]
    sub ax, 66
    jns .yok
    xor ax, ax
.yok:
    shr ax, 1
    add ax, [apu_oy]
    mov [apab_y1], ax              ; y1
    ; --- white fill ---
    mov al, 15
    call OSAPI_SET_COLOR
    mov ax, [apab_x1]
    mov bx, [apab_y1]
    mov cx, [apu_ox]
    add cx, [apu_cw]
    sub cx, 9                      ; x2
    mov dx, bx
    add dx, 65                     ; y2
    call OSAPI_GFX_FILL
    ; --- black frame ---
    mov al, 0
    call OSAPI_SET_COLOR
    mov ax, [apab_x1]
    mov bx, [apab_y1]
    mov cx, [apu_ox]
    add cx, [apu_cw]
    sub cx, 9
    mov dx, bx
    add dx, 65
    call OSAPI_GFX_FRAME
    ; --- four lines (ink 0 on paper 15) ---
    mov si, apu_ab1
    mov cx, [apab_x1]
    add cx, 8
    mov dx, [apab_y1]
    add dx, 7
    mov ax, 0x0F00
    call OSAPI_FONT_RUN
    mov si, apu_ab2
    mov cx, [apab_x1]
    add cx, 8
    mov dx, [apab_y1]
    add dx, 21
    mov ax, 0x0F00
    call OSAPI_FONT_RUN
    mov si, apu_ab3
    mov cx, [apab_x1]
    add cx, 8
    mov dx, [apab_y1]
    add dx, 35
    mov ax, 0x0F00
    call OSAPI_FONT_RUN
    mov si, apu_ab4
    mov cx, [apab_x1]
    add cx, 8
    mov dx, [apab_y1]
    add dx, 49
    mov ax, 0x0F00
    call OSAPI_FONT_RUN
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; apu_draw_stats - the Diagnostics view (SPEC.md 86.5.1). Formatted ONCE per
; repaint, which is a rare event (a key press, or a queue add) - never in the
; hot path. Built only with -DAPROF; otherwise a one-line note.
; =============================================================================
apu_draw_stats:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
%ifndef APROF
    mov si, aps_none
    mov cx, [apu_ox]
    add cx, 6
    mov dx, [apu_oy]
    add dx, 6
    mov ax, 0x0F00
    call OSAPI_FONT_RUN
    jmp .out
%else
    mov word [aps_row], 4
    mov si, aps_title
    call aps_line
    ; ---- one label + one number per row --------------------------------
    mov si, aps_l_iter
    mov ax, [apc_iter]
    call aps_kv
    mov si, aps_l_stat
    mov ax, [apc_status]
    call aps_kv
    mov si, aps_l_stag
    mov ax, [apc_stage]
    call aps_kv
    mov si, aps_l_feed
    mov ax, [apc_feed]
    call aps_kv
    mov si, aps_l_wake
    mov ax, [apc_wake]
    call aps_kv
    mov si, aps_l_onwk
    mov ax, [apc_onwake]
    call aps_kv
    mov si, aps_l_read
    mov ax, [apc_readat]
    call aps_kv
    mov si, aps_l_rby
    mov ax, [apc_rbytes]
    mov dx, [apc_rbytes+2]
    call aps_kv32
    mov si, aps_l_refl
    mov ax, [apc_refill]
    call aps_kv
    mov si, aps_l_undr
    mov ax, [apc_under]
    call aps_kv
    mov si, aps_l_wend
    mov ax, [apc_wend]
    call aps_kv
    mov si, aps_l_uip
    mov ax, [apc_uipaint]
    call aps_kv
    mov si, aps_l_prg
    mov ax, [apc_progupd]
    call aps_kv
    mov si, aps_l_dec
    mov ax, [apc_declc]
    call aps_kv
    mov si, aps_l_dby
    mov ax, [apc_decby]
    mov dx, [apc_decby+2]
    call aps_kv32
    mov si, aps_l_que
    mov ax, [apc_quechk]
    call aps_kv
    mov si, aps_l_open
    mov ax, [apc_opentk]
    call aps_kv
    mov si, aps_l_pars
    mov ax, [apc_parse]
    call aps_kv
    mov si, aps_l_slid
    mov ax, [apc_slide]
    call aps_kv
    mov si, aps_l_prim
    mov ax, [apc_prime]
    call aps_kv
    mov si, aps_l_pdsc
    mov ax, [apc_pdisc]
    call aps_kv
    mov si, aps_hint
    call aps_line
%endif
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

%ifndef APROF
aps_none: db 'Diagnostics: rebuild with -DAPROF', 0
%else
    APW aps_row
    APBUF aps_num, 12
aps_title: db 'Audio Player diagnostics  (D to hide)', 0
aps_hint:  db '(press D twice to refresh)', 0
aps_l_iter: db 'worker iters  ', 0
aps_l_stat: db 'status polls  ', 0
aps_l_stag: db 'stage (v6)    ', 0
aps_l_feed: db 'feed (v1)     ', 0
aps_l_wake: db 'WM_WAKE       ', 0
aps_l_onwk: db 'WM_ONWAKE     ', 0
aps_l_read: db 'FILE_READ_AT  ', 0
aps_l_rby:  db 'bytes read    ', 0
aps_l_refl: db 'refill events ', 0
aps_l_undr: db 'underruns     ', 0
aps_l_wend: db 'ended/watchdog', 0
aps_l_uip:  db 'full repaints ', 0
aps_l_prg:  db 'progress upd  ', 0
aps_l_dec:  db 'decoder calls ', 0
aps_l_dby:  db 'decoded bytes ', 0
aps_l_que:  db 'queue polls   ', 0
aps_l_open: db 'open_track    ', 0
aps_l_pars: db 'wav parse     ', 0
aps_l_slid: db 'wav slide     ', 0
aps_l_prim: db 'prime         ', 0
aps_l_pdsc: db 'input-starved ', 0

; aps_line - SI = string; draw it at the current row, advance the row by 8
aps_line:
    push ax
    push cx
    push dx
    mov cx, [apu_ox]
    add cx, 6
    mov dx, [apu_oy]
    add dx, [aps_row]
    mov ax, 0x0F00
    call OSAPI_FONT_RUN
    add word [aps_row], 8
    pop dx
    pop cx
    pop ax
    ret

; aps_kv - SI = label, AX = value; one row "label   NNNNN", then row += 8
aps_kv:
    push di
    mov di, aps_num
    call aps_u16
    call aps_kv_emit
    pop di
    ret

; aps_kv32 - SI = label, DX:AX = value
aps_kv32:
    push di
    mov di, aps_num
    call aps_u32
    call aps_kv_emit
    pop di
    ret

; aps_kv_emit - SI = label, aps_num holds the formatted value
aps_kv_emit:
    push ax
    push bx
    push cx
    push dx
    push si
    mov cx, [apu_ox]
    add cx, 6
    mov dx, [apu_oy]
    add dx, [aps_row]
    mov ax, 0x0F00
    call OSAPI_FONT_RUN            ; the label (SI)
    mov si, aps_num
    mov cx, [apu_ox]
    add cx, 6 + 110
    mov dx, [apu_oy]
    add dx, [aps_row]
    mov ax, 0x0F00
    call OSAPI_FONT_RUN            ; the value
    add word [aps_row], 8
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; aps_u16 - AX unsigned -> decimal NUL string at DI
aps_u16:
    ; STKBALANCE-LOOP: pushes one digit per loop turn, pops them in the second loop - an itoa
    push ax
    push bx
    push cx
    push dx
    mov bx, 10
    xor cx, cx
.d:
    xor dx, dx
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .d
.e:
    pop dx
    add dl, '0'
    mov [di], dl
    inc di
    loop .e
    mov byte [di], 0
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; aps_u32 - DX:AX unsigned -> decimal NUL string at DI
aps_u32:
    ; STKBALANCE-LOOP: the 32-bit itoa: a digit pushed per turn, popped in the second loop
    push ax
    push bx
    push cx
    push dx
    push si
    xor cx, cx                     ; digit count
    mov si, 10
.d:
    ; DX:AX / 10 -> DX:AX quotient, BX remainder
    push ax
    mov ax, dx
    xor dx, dx
    div si                         ; AX = hi quotient, DX = hi rem
    mov bx, ax
    pop ax
    div si                         ; AX = lo quotient, DX = remainder
    push dx                        ; the digit
    mov dx, bx                     ; carry the hi quotient
    inc cx
    mov bx, ax
    or bx, dx
    jnz .d
.e:
    pop dx
    add dl, '0'
    mov [di], dl
    inc di
    loop .e
    mov byte [di], 0
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; ap_diag_dump - write every counter to APDIAG.TXT on the system-volume root,
; one "name value" per line, so the host can read exact numbers off the image
; instead of squinting at a 8px on-screen column. APROF only; not shipped.
; UI-task context (the 'D' key). Saves and restores the current directory.
; =============================================================================
apdg_nl: db 10, 0

; apdg_s - append the NUL string at SI to [DI], DI walks
apdg_s:
    push ax
.l:
    mov al, [si]
    or al, al
    jz .d
    mov [di], al
    inc di
    inc si
    jmp .l
.d:
    pop ax
    ret

; apdg_num - append decimal AX then newline at [DI], DI walks
apdg_num:
    ; STKBALANCE-LOOP: pushes one digit per loop turn, pops them in the second loop - an itoa
    push ax
    push bx
    push cx
    push dx
    mov bx, 10
    xor cx, cx
.dv:
    xor dx, dx
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .dv
.em:
    pop dx
    add dl, '0'
    mov [di], dl
    inc di
    loop .em
    mov byte [di], 10
    inc di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; apdg_num32 - append decimal DX:AX then newline at [DI], DI walks
apdg_num32:
    ; STKBALANCE-LOOP: the 32-bit itoa: a digit pushed per turn, popped in the second loop
    push ax
    push bx
    push cx
    push dx
    push si
    xor cx, cx
    mov si, 10
.dv:
    push ax
    mov ax, dx
    xor dx, dx
    div si
    mov bx, ax
    pop ax
    div si
    push dx
    mov dx, bx
    inc cx
    mov bx, ax
    or bx, dx
    jnz .dv
.em:
    pop dx
    add dl, '0'
    mov [di], dl
    inc di
    loop .em
    mov byte [di], 10
    inc di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

%macro APDGV 2
    mov si, %1
    call apdg_s
    mov ax, [%2]
    call apdg_num
%endmacro
%macro APDGV32 2
    mov si, %1
    call apdg_s
    mov ax, [%2]
    mov dx, [%2 + 2]
    call apdg_num32
%endmacro

apdg_fname: db 'APDIAG.TXT', 0
apdg_l_iter:  db 'iter ', 0
apdg_l_stat:  db 'status ', 0
apdg_l_stag:  db 'stage ', 0
apdg_l_feed:  db 'feed ', 0
apdg_l_wake:  db 'wmwake ', 0
apdg_l_onwk:  db 'onwake ', 0
apdg_l_read:  db 'readat ', 0
apdg_l_rby:   db 'rbytes ', 0
apdg_l_refl:  db 'refill ', 0
apdg_l_undr:  db 'under ', 0
apdg_l_wend:  db 'wend ', 0
apdg_l_uip:   db 'uipaint ', 0
apdg_l_prg:   db 'progupd ', 0
apdg_l_dec:   db 'declc ', 0
apdg_l_dby:   db 'decby ', 0
apdg_l_que:   db 'quechk ', 0
apdg_l_open:  db 'opentk ', 0
apdg_l_pars:  db 'parse ', 0
apdg_l_slid:  db 'slide ', 0
apdg_l_prim:  db 'prime ', 0
apdg_l_pdsc:  db 'inpstarved ', 0
apdg_l_rate:  db 'rate ', 0
apdg_l_doff:  db 'dataoff ', 0

ap_diag_dump:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    call OSAPI_FILE_HERE           ; DX = cwd cluster, BL = drive
    mov [ap_que_bdir], dx
    mov [ap_que_bvol], bl

    mov di, apdg_buf
    APDGV   apdg_l_iter, apc_iter
    APDGV   apdg_l_stat, apc_status
    APDGV   apdg_l_stag, apc_stage
    APDGV   apdg_l_feed, apc_feed
    APDGV   apdg_l_wake, apc_wake
    APDGV   apdg_l_onwk, apc_onwake
    APDGV   apdg_l_read, apc_readat
    APDGV32 apdg_l_rby,  apc_rbytes
    APDGV   apdg_l_refl, apc_refill
    APDGV   apdg_l_undr, apc_under
    APDGV   apdg_l_wend, apc_wend
    APDGV   apdg_l_uip,  apc_uipaint
    APDGV   apdg_l_prg,  apc_progupd
    APDGV   apdg_l_dec,  apc_declc
    APDGV32 apdg_l_dby,  apc_decby
    APDGV   apdg_l_que,  apc_quechk
    APDGV   apdg_l_open, apc_opentk
    APDGV   apdg_l_pars, apc_parse
    APDGV   apdg_l_slid, apc_slide
    APDGV   apdg_l_prim, apc_prime
    APDGV   apdg_l_pdsc, apc_pdisc
    APDGV   apdg_l_rate, ap_rate       ; parsed values, to catch a bad parse
    APDGV32 apdg_l_doff, ap_data_off
    mov si, di
    mov ax, si
    sub ax, apdg_buf
    mov [apdg_t0], ax             ; reuse: byte count for the write

    call ap_que_root             ; -> system volume root (clobbers AX/BX/DX)
    push ds
    pop es
    mov si, apdg_fname
    mov bx, apdg_buf
    xor dx, dx
    mov cx, [apdg_t0]
    call OSAPI_FILE_WRITE         ; whole-file write: creates / overwrites

    mov dx, [ap_que_bdir]
    mov bl, [ap_que_bvol]
    call OSAPI_FILE_GOTO          ; back to the track's directory (real remount)
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
%endif

; --- apu_draw_time_full - the "MM:SS [bar] MM:SS" row, all of it -------------
apu_draw_time_full:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, [ap_elapsed_s]
    mov di, apu_t1
    call apu_time_str
    mov ax, [ap_total_s]
    mov di, apu_t2
    call apu_time_str

    mov si, apu_t1
    mov cx, [apu_ox]
    add cx, 6
    mov dx, [apu_oy]
    add dx, 31
    mov ax, 0x0F00                 ; FONT_RUN: ink 0 on paper 15 (SPEC.md 6.6)
    call OSAPI_FONT_RUN

    mov si, apu_t2
    mov cx, [apu_ox]
    add cx, [apu_cw]
    sub cx, 40
    mov dx, [apu_oy]
    add dx, 31
    mov ax, 0x0F00                 ; FONT_RUN: ink 0 on paper 15 (SPEC.md 6.6)
    call OSAPI_FONT_RUN

    ; bar frame
    mov ax, [apu_ox]
    add ax, [apu_pbx1]
    mov bx, [apu_oy]
    add bx, [apu_pby1]
    mov cx, [apu_ox]
    add cx, [apu_pbx2]
    mov dx, [apu_oy]
    add dx, [apu_pby2]
    call OSAPI_GFX_FRAME

    call apu_draw_bar_fill

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- apu_draw_bar_fill - clear the bar interior, then fill the played part ---
apu_draw_bar_fill:
    push ax
    push bx
    push cx
    push dx
    ; interior white
    mov ax, [apu_ox]
    add ax, [apu_pbx1]
    inc ax
    mov bx, [apu_oy]
    add bx, [apu_pby1]
    inc bx
    mov cx, [apu_ox]
    add cx, [apu_pbx2]
    dec cx
    mov dx, [apu_oy]
    add dx, [apu_pby2]
    dec dx
    push ax
    push cx
    call apu_penw     ; helper below
    call OSAPI_GFX_FILL
    pop cx
    pop ax
    ; filled part
    call apu_progress             ; AX = filled px
    or ax, ax
    jz .done
    push ax
    mov ax, [apu_ox]
    add ax, [apu_pbx1]
    inc ax                        ; x1
    pop cx
    add cx, ax                    ; x2 = x1 + filled
    mov bx, [apu_oy]
    add bx, [apu_pby1]
    inc bx
    mov dx, [apu_oy]
    add dx, [apu_pby2]
    dec dx
    call apu_penb
    call OSAPI_GFX_FILL
.done:
    call apu_penb
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- apu_draw_bar_delta - the tick's bar update: only the strip it grew by ---
; The bar never shrinks while a track plays (no seeking, SPEC.md 86.20), so
; the pixels left of [ap_last_bar] are already black and those right of the
; new edge already white: one fill, never white-then-black (PERFORMANCE.md
; rule 2). A bar that is starting (last = 0) or went backwards (a new track)
; takes the full apu_draw_bar_fill.
apu_draw_bar_delta:
    push ax
    push bx
    push cx
    push dx
    call apu_progress             ; AX = filled px now
    mov bx, [ap_last_bar]         ; ...and at the last draw
    or bx, bx
    jz .full
    cmp ax, bx
    jb .full
    je .done                      ; only the clock moved
    mov cx, [apu_ox]
    add cx, [apu_pbx1]
    inc cx                        ; x1 of the interior
    add ax, cx                    ; x2 = x1 + new
    add cx, bx
    inc cx                        ; x1 + old + 1
    xchg ax, cx                   ; AX = left edge, CX = right edge
    mov bx, [apu_oy]
    add bx, [apu_pby1]
    inc bx
    mov dx, [apu_oy]
    add dx, [apu_pby2]
    dec dx
    call apu_penb
    call OSAPI_GFX_FILL
    jmp .done
.full:
    call apu_draw_bar_fill
.done:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

apu_penw:
    push ax
    mov al, 15
    call OSAPI_SET_COLOR
    pop ax
    ret
apu_penb:
    push ax
    xor al, al
    call OSAPI_SET_COLOR
    pop ax
    ret

; --- apu_draw_dyn - the cheap update the worker runs each pass --------------
apu_draw_dyn:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call apu_origin
    ; erase + redraw the elapsed field (opaque run)
    mov ax, [ap_elapsed_s]
    mov di, apu_t1
    call apu_time_str
    mov si, apu_t1
    mov cx, [apu_ox]
    add cx, 6
    mov dx, [apu_oy]
    add dx, 31
    mov al, 0                      ; ink black
    mov ah, 15                     ; paper white
    call OSAPI_FONT_RUN
    call apu_draw_bar_delta
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- apu_draw_buttons -----------------------------------------------------
apu_draw_buttons:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor di, di
.b:
    cmp di, 7
    jae .done
    ; rect -> absolute in a local 4-word buffer
    mov bx, di
    mov cl, 3
    shl bx, cl
    add bx, apu_rects
    mov ax, [bx+0]
    add ax, [apu_ox]
    mov [apu_abr+0], ax
    mov ax, [bx+2]
    add ax, [apu_oy]
    mov [apu_abr+2], ax
    mov ax, [bx+4]
    add ax, [apu_ox]
    mov [apu_abr+4], ax
    mov ax, [bx+6]
    add ax, [apu_oy]
    mov [apu_abr+6], ax
    ; label + flags
    mov si, di
    add si, si
    mov si, [apu_blabs + si]
    xor cx, cx                     ; CX = OS88UI_* flags (via DI later)
    ; Shuffle / Repeat show the pressed look when toggled on
    cmp di, AP_B_SHUF
    jne .nsh
    cmp byte [ap_shuffle], 0
    je .nsh
    mov cx, OS88UI_DOWN
.nsh:
    cmp di, AP_B_REP
    jne .ndr
    cmp byte [ap_repeat], 0
    je .ndr
    mov cx, OS88UI_DOWN
.ndr:
    push di
    mov bx, apu_abr
    mov di, cx
    call os88ui_btn                ; BX = rect, SI = label, DI = flags
    pop di
    inc di
    jmp .b
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

apu_blabs:
    dw ap_l_prev, ap_l_play, ap_l_pause, ap_l_stop, ap_l_next, ap_l_shuf, ap_l_rep

; --- apu_draw_list - the playlist, one 8px row per entry -------------------
apu_draw_list:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, apu_l_plhdr
    mov cx, [apu_ox]
    add cx, 6
    mov dx, [apu_oy]
    add dx, 68
    mov ax, 0x0F00                 ; FONT_RUN: ink 0 on paper 15 (SPEC.md 6.6)
    call OSAPI_FONT_RUN

    call apl_count
    mov [apu_ln], ax
    xor di, di                     ; row index
.row:
    cmp di, [apu_ln]
    jae .done
    mov ax, di
    mov bx, 8
    mul bx
    add ax, 78
    cmp ax, [apu_ch]
    jae .done                      ; below the content
    mov dx, [apu_oy]
    add dx, ax                     ; DX = row y
    mov cx, [apu_ox]
    add cx, 8
    ; marker for the current entry
    mov si, apu_sp
    mov ax, di
    cmp al, [apl_cur]
    jne .nomark
    mov si, apu_gt
.nomark:
    mov ax, 0x0F00                 ; FONT_RUN: ink 0 on paper 15 (SPEC.md 6.6)
    call OSAPI_FONT_RUN
    ; the name
    mov ax, di
    call apl_entptr               ; BX = &entry
    mov si, bx
    mov cx, [apu_ox]
    add cx, 20
    mov ax, 0x0F00                 ; FONT_RUN: ink 0 on paper 15 (SPEC.md 6.6)
    call OSAPI_FONT_RUN
    inc di
    jmp .row
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

apu_sp: db ' ', 0
apu_gt: db '>', 0

; =============================================================================
; apu_hit - a content-relative click point -> AL = AP_B_* or AP_B_NONE
; in:  CX = x (content-relative), DX = y (content-relative)
;
; The buttons tile the row gap-free: column = (x - 4) / pitch, so a click
; anywhere on the row lands on a button and there is no 3px dead lane between
; two of them (found on the glass).
; =============================================================================
apu_hit:
    push bx
    push dx
    ; y must be on the transport row (rects[0] carries y1/y2 for all seven)
    cmp dx, [apu_rects+2]
    jb .none
    cmp dx, [apu_rects+6]
    ja .none
    mov ax, cx
    cmp ax, [apu_rects+0]              ; left edge of button 0
    jb .none
    sub ax, [apu_rects+0]
    cmp word [apu_bpitch], 0
    je .none
    xor dx, dx
    div word [apu_bpitch]             ; AX = column
    cmp ax, 7
    jae .none
    pop dx
    pop bx
    ret
.none:
    mov ax, AP_B_NONE
    pop dx
    pop bx
    ret

    APBUF apu_abr, 8               ; scratch absolute button rect
    APW   apu_bpitch
    APW   apu_ln
