; =============================================================================
; os8088 - apps/audio/apengine.inc
;
; The Audio Player's streaming engine (SPEC.md 86.5). Everything here runs on
; the UI task EXCEPT ap_la_get / ap_la_avail / ap_src_eof, which the worker
; also calls and which touch only the free-running ring counters.
;
;   look-ahead ring   ap_la_get / ap_ring_put over a 32 KB heap claim, with
;                     free-running 16-bit ap_la_wr / ap_la_rd (SPEC.md 86.5)
;   track open        apl_goto_cur -> apw_parse -> claim -> grant -> pre-roll
;                     -> OSAPI_SND_STREAM verb 0 (ring mode)
;   refill            ap_refill_chunk: one cluster read, head-skipped and
;                     tail-clamped into the ring (UI task only, SPEC.md 20.6/7)
;   close / reap      ap_stream_close drains [ap_mixing] first (SPEC.md 45.2)
; =============================================================================

    APBUF ap_findbuf, 24           ; OSAPI_FIND_SZ
    APW  ap_lg_got                 ; scratch: ap_la_get byte count
    APW  ap_rp_n                   ; scratch: ap_ring_put byte count

; =============================================================================
; ap_la_avail -> AX = bytes queued in the look-ahead ring
; preserves every register except AX.
; =============================================================================
ap_la_avail:
    mov ax, [ap_la_wr]
    sub ax, [ap_la_rd]
    ret

; =============================================================================
; ap_src_eof -> CF = 1 when no more audio will ever arrive
; preserves every register.
; =============================================================================
ap_src_eof:
    push ax
    cmp byte [ap_src_done], 0
    je .no
    mov ax, [ap_la_wr]
    cmp ax, [ap_la_rd]
    jne .no
    pop ax
    stc
    ret
.no:
    pop ax
    clc
    ret

; =============================================================================
; ap_la_get - copy up to CX bytes out of the look-ahead ring into DS:DI
; in:  CX = max bytes, DI = destination offset (in this segment)
; out: AX = bytes actually copied; ap_la_rd advanced by AX
; clobbers: nothing else (all pushed).  ANY-TASK.
; =============================================================================
ap_la_get:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    mov ax, [ap_la_wr]
    sub ax, [ap_la_rd]
    cmp ax, cx
    jbe .g1
    mov ax, cx
.g1:
    mov [ap_lg_got], ax
    or ax, ax
    jz .ret
    mov si, [ap_la_rd]
    and si, AP_LA_MASK
    mov bx, [ap_la_seg]
    mov dx, AP_LA_SZ
    sub dx, si                     ; DX = contiguous run to the ring seam
    mov cx, [ap_lg_got]
    push ds
    pop es                         ; ES = this segment (the destination)
    mov ds, bx                     ; DS = ring segment
    cld
    cmp cx, dx
    jbe .single
    push cx
    mov cx, dx
    rep movsb
    pop cx
    sub cx, dx
    xor si, si
    rep movsb
    jmp .fin
.single:
    rep movsb
.fin:
    pop ds                         ; restore this segment
    pop es
    mov ax, [ap_lg_got]
    add [ap_la_rd], ax
    jmp .out
.ret:
    pop ds
    pop es
.out:
    mov ax, [ap_lg_got]
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; =============================================================================
; ap_ring_put - copy CX bytes from DS:SI into the look-ahead ring
; in:  SI = source offset (this segment), CX = bytes (<= AP_LA_SZ)
; out: ap_la_wr advanced by CX
; clobbers: nothing (all pushed).  UI task only.
; =============================================================================
ap_ring_put:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov [ap_rp_n], cx
    mov ax, [ap_la_seg]
    mov es, ax
    mov di, [ap_la_wr]
    and di, AP_LA_MASK
    mov dx, AP_LA_SZ
    sub dx, di                     ; DX = run to the ring seam
    cld
    cmp cx, dx
    jbe .single
    push cx
    mov cx, dx
    rep movsb
    pop cx
    sub cx, dx
    xor di, di
    rep movsb
    jmp .fin
.single:
    rep movsb
.fin:
    mov ax, [ap_rp_n]
    add [ap_la_wr], ax
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; ap_filesize - find a file by name in the current directory
; in:  SI -> NUL 8.3 name (this segment)
; out: CF = 0 and DX:AX = size in bytes; CF = 1 not found
; clobbers: AX, CX, DX (SI preserved)
; =============================================================================
ap_filesize:
    push bx
    push si
    push di
    push es
    push ds
    pop es
    xor cx, cx                     ; ordinal 0
.loop:
    mov di, ap_findbuf
    call OSAPI_FILE_FIND           ; CF=1 AX=FERR_NOENT at the end
    jc .none
    ; compare ap_findbuf[0..] with the name at [ss? no - SI in this seg]
    push si
    push cx
    mov di, ap_findbuf
    call ap_nameeq                 ; ZF = 1 if equal (case-insensitive)
    pop cx
    pop si
    je .hit
    jmp .loop
.hit:
    mov ax, [ap_findbuf+18]
    mov dx, [ap_findbuf+20]
    pop es
    pop di
    pop si
    pop bx
    clc
    ret
.none:
    pop es
    pop di
    pop si
    pop bx
    stc
    ret

; ap_nameeq - compare NUL strings DS:SI and DS:DI, ASCII case-insensitive
;             ZF = 1 when equal.  preserves every register except flags.
ap_nameeq:
    push si
    push di
    push ax
    push bx
.l:
    mov al, [si]
    mov bl, [di]
    call ap_upcase                 ; AL
    xchg al, bl
    call ap_upcase
    xchg al, bl
    cmp al, bl
    jne .ne
    or al, al
    jz .eq
    inc si
    inc di
    jmp .l
.eq:
    xor al, al                     ; ZF = 1
    jmp .done
.ne:
    or al, 1                       ; ZF = 0
.done:
    pop bx
    pop ax
    pop di
    pop si
    ret

; ap_upcase - AL: 'a'..'z' -> 'A'..'Z'
ap_upcase:
    cmp al, 'a'
    jb .r
    cmp al, 'z'
    ja .r
    sub al, 0x20
.r:
    ret

; =============================================================================
; ap_ensure_bufs - claim the 32 KB look-ahead buffer once, held for our life
; out: CF = 1 if the heap could not fund it
; =============================================================================
ap_ensure_bufs:
    cmp word [ap_la_seg], 0
    jne .ok
    mov ax, 32                     ; AP_LA_SZ / 1024
    call OSAPI_MEM_CLAIM           ; out CF, DX = base segment
    jc .fail
    mov [ap_la_seg], dx
.ok:
    clc
    ret
.fail:
    stc
    ret

; =============================================================================
; ap_grant_ring - take a staging-pool grant for the SND ring (verb 7), tiered
; out: CF = 1 refused; else ap_grant / ap_ring / ap_rmask / ap_ghave set
; =============================================================================
ap_grant_ring:
    cmp byte [ap_ghave], 0
    jne .have
    mov cx, AP_RING_KB * 1024
.try:
    mov al, 7
    mov ah, 0                      ; sub-op 0 = alloc
    call OSAPI_SND_STREAM          ; out AX = 0 ok, SI = grant offset
    or ax, ax
    jz .got
    cmp cx, AP_RING_SM * 1024
    jbe .no
    mov cx, AP_RING_SM * 1024
    jmp .try
.got:
    mov [ap_grant], si
    mov [ap_ring], cx
    mov ax, cx
    dec ax
    mov [ap_rmask], ax
    mov ax, cx                     ; the pre-roll follows the ring: halves =
    mov cl, 11                     ; ring / AP_HALF - 1, capped at AP_PREROLL -
    shr ax, cl                     ; 6 on the 16 KB tier, 3 on the 8 KB one.
    dec ax                         ; A fixed six (12 KB) is more than the small
    cmp ax, AP_PREROLL             ; ring holds, and verb 0 refuses CX > the
    jbe .pr                        ; ring, so that tier could never play (the
    mov ax, AP_PREROLL             ; Tracker's trk_ring_set rule)
.pr:
    mov [ap_preroll], ax
    mov byte [ap_ghave], 1
.have:
    clc
    ret
.no:
    stc
    ret

; =============================================================================
; ap_free_ring - hand the grant back (verb 7 free) so the driver can release
;                its staging pool while we are stopped (SPEC.md 34.6)
; =============================================================================
ap_free_ring:
    cmp byte [ap_ghave], 0
    je .out
    push si
    mov si, [ap_grant]
    mov al, 7
    mov ah, 1                      ; sub-op 1 = free
    call OSAPI_SND_STREAM
    pop si
    or ax, ax
    jnz .out                       ; refused: keep the latch, reuse next time
    mov byte [ap_ghave], 0
.out:
    ret

; =============================================================================
; ap_refill_chunk - read ONE cluster of the data chunk into the ring, with the
;                   first chunk's head skipped and the last chunk's tail
;                   clamped at ap_data_end.  UI task only.
; out: nothing (ap_src_done latched when the whole chunk has been read)
; =============================================================================
ap_refill_chunk:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cmp byte [ap_src_done], 0
    jne .out
    ; --- how big a gulp? min(AP_RD_CHUNK, free) floored to a cluster ---------
    ; A read holds sch_lock and costs ~1-2 disk revolutions whatever it moves
    ; (SPEC.md 34.5 / 86.5.2), so we want the FEWEST, biggest reads the ring
    ; has room for - not one cluster at a time.
    call ap_la_avail
    mov bx, AP_LA_SZ
    sub bx, ax                     ; BX = free bytes in the ring
    cmp bx, AP_RD_CHUNK
    jbe .cap
    mov bx, AP_RD_CHUNK
.cap:
    mov ax, [ap_clu]
    dec ax
    not ax
    and bx, ax                     ; floor to a whole number of clusters
    cmp bx, [ap_clu]
    jb .out                        ; not room for even one cluster - later
    mov [ap_rd_len], bx

    ; still standing in the track's folder? The Open dialog moves the
    ; instance to whatever folder the user picked (SPEC.md 38.10), and the
    ; name below resolves THERE - not found reads as EOF, a same-named file
    ; as audio. GOTO back first; only a user who browsed away pays it.
    call OSAPI_FILE_HERE
    cmp dx, [ap_trk_dir]
    jne .goback
    cmp bl, [ap_trk_vol]
    je .inplace
.goback:
    mov dx, [ap_trk_dir]
    mov bl, [ap_trk_vol]
    call OSAPI_FILE_GOTO_QM        ; the quiet stand that MOVES us: a word
    jc .ioerr                      ; compare inside one volume, a quiet mount
                                   ; across; CF: the folder is gone - the track
                                   ; ends, and says why
.inplace:
    ; read [ap_rd_pos, +ap_rd_len) into ap_scratch
    push ds
    pop es
    mov si, [apw_name]
    mov bx, ap_scratch
    mov cx, [ap_rd_len]
    mov ax, [ap_rd_pos]
    mov dx, [ap_rd_pos+2]
    call OSAPI_FILE_READ_AT        ; out CF, DX:AX = bytes delivered
    APCNT apc_readat
    APCNT apc_refill
    jc .ioerr                     ; an I/O error ends the data - and says why
    APADD apc_rbytes, ax
    ; DX:AX = got; it fits a word (AP_RD_CHUNK <= 16384)
    mov bx, ax                     ; BX = got (low word; DX must be 0)
    or dx, dx
    jz .g0
    mov bx, [ap_rd_len]
.g0:
    or bx, bx
    jz .done_all                  ; nothing delivered -> at/after EOF

    ; usable = got - head-skip, then clamp so we never pass ap_data_end
    mov si, ap_scratch
    mov cx, bx                     ; CX = usable count
    mov ax, [ap_skip]
    or ax, ax
    jz .noskip
    cmp ax, cx
    jae .allskip
    add si, ax
    sub cx, ax
    mov word [ap_skip], 0
    jmp .clamp
.allskip:
    sub word [ap_skip], bx        ; whole chunk was pre-data - carry the skip
    jmp .advance
.noskip:
.clamp:
    ; bytes of real audio still wanted = ap_data_end - (ap_data_off + ap_got)
    mov ax, [ap_data_off]
    mov dx, [ap_data_off+2]
    add ax, [ap_got]
    adc dx, [ap_got+2]            ; DX:AX = file offset of the next audio byte
    mov bx, [ap_data_end]
    mov di, [ap_data_end+2]
    sub bx, ax
    sbb di, dx                     ; DI:BX = bytes of data left
    or di, di
    jnz .putall                   ; more than 64 KB left - CX is fine as is
    cmp bx, cx
    jae .putall
    mov cx, bx                     ; clamp to the tail
.putall:
    or cx, cx
    jz .advance
    call ap_ring_put              ; SI = source, CX = bytes
    ; ap_got += CX
    xor dx, dx
    mov ax, cx
    add [ap_got], ax
    adc [ap_got+2], dx
.advance:
    ; ap_rd_pos += ap_rd_len
    mov ax, [ap_rd_len]
    xor dx, dx
    add [ap_rd_pos], ax
    adc [ap_rd_pos+2], dx
    ; done if the next read position is at/after the data end
    mov ax, [ap_rd_pos]
    mov dx, [ap_rd_pos+2]
    cmp dx, [ap_data_end+2]
    jb .out
    ja .done_all
    cmp ax, [ap_data_end]
    jb .out
.ioerr:
    mov word [ap_msg], apw_e_io    ; a bad sector or a pulled disk: the status
                                   ; line says so, not "End of playlist"
.done_all:
    mov byte [ap_src_done], 1
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; ap_prime - fill the look-ahead ring before playback starts (UI context, so
;            the sch_lock cost is paid here, not while the DSP is running).
; =============================================================================
ap_prime:
    APCNT apc_prime
    push cx
    mov cx, 24                     ; hard cap on read iterations
.l:
    cmp byte [ap_src_done], 0
    jne .done
    call ap_la_avail
    ; one AP_RD_CHUNK gulp is enough to start - it covers the six-half pre-roll
    ; with headroom, and the worker tops the ring up once playing. Filling the
    ; whole 32 KB here was a second disk read in the "Opening..." wait, which
    ; on a real XT is a visible extra second per track.
    cmp ax, AP_RD_CHUNK - AP_HALF  ; less the head skip: data never starts on
    jae .done                      ; a cluster boundary, so a full gulp is
    call ap_refill_chunk           ; never quite AP_RD_CHUNK of audio
    loop .l
.done:
    pop cx
    ret

; =============================================================================
; ap_open_track - open and start the current playlist entry
; out: CF = 0 playing; CF = 1 refused (ap_msg set to the reason)
; clobbers: AX..DI (top-level, from a callback or ap_onwake)
; =============================================================================
ap_open_track:
    APCNT apc_opentk
    cmp byte [ap_have_sb], 0
    jne .havesb
    mov word [ap_msg], ap_s_nosb
    jmp .refuse
.havesb:
    call apl_count
    or ax, ax
    jnz .havetrk
    mov word [ap_msg], ap_s_nofile
    jmp .refuse
.havetrk:
    mov word [ap_msg], ap_s_opening

    call ap_stream_close          ; a clean slate (drains the worker)

    call apl_goto_cur             ; move into the track's directory
    jc .badfile                   ; could not reach the folder - say so
    call OSAPI_FILE_HERE          ; ...and bank it: the refill reads BY NAME,
    mov [ap_trk_dir], dx          ; and a File > Open in another folder moves
    mov [ap_trk_vol], bl          ; the instance there (SPEC.md 38.10, 86.3)
    call apl_cur_name             ; SI = name
    jc .badfile

    call ap_filesize             ; CF, DX:AX = size
    jc .badfile

    call apw_parse                ; CF = 1 rejected, apw_err set
    jc .parseerr

    ; --- adopt the parsed record ---------------------------------------
    mov al, [apw_fmt]
    mov cx, [apw_balign]
    call apd_start

    mov ax, [apw_rate]
    mov [ap_rate], ax

    mov ax, [apw_datoff]
    mov dx, [apw_datoff+2]
    mov [ap_data_off], ax
    mov [ap_data_off+2], dx
    add ax, [apw_datlen]
    adc dx, [apw_datlen+2]
    mov [ap_data_end], ax
    mov [ap_data_end+2], dx

    mov ax, [apw_clu]
    mov [ap_clu], ax
    ; ap_rd_pos = data_off rounded down to cluster ; ap_skip = data_off & (clu-1)
    mov ax, [apw_datoff]
    mov dx, [apw_datoff+2]
    mov bx, [ap_clu]
    dec bx
    mov cx, ax
    and cx, bx                     ; low bits
    mov [ap_skip], cx
    not bx
    and ax, bx
    mov [ap_rd_pos], ax
    mov [ap_rd_pos+2], dx

    mov word [ap_got], 0
    mov word [ap_got+2], 0
    mov byte [ap_src_done], 0
    mov word [ap_la_wr], 0
    mov word [ap_la_rd], 0
    mov word [ap_total], 0
    mov word [ap_consumed], 0
    mov word [ap_consumed_prev], 0
    mov word [ap_played], 0
    mov word [ap_played+2], 0
    mov byte [ap_ended], 0
    mov byte [ap_lasthalf], 0
    mov byte [ap_paused], 0
    mov byte [ap_req], FR_NONE
    mov word [ap_elapsed_s], 0
    mov word [ap_sub], 0

    call ap_calc_duration         ; ap_total_s from datlen / rate / bytes-per-s
    call ap_calc_bpt              ; ap_bpt = rate / 18.2065

    call ap_ensure_bufs
    jc .nomem
    call ap_grant_ring
    jc .nomem

    call ap_prime                 ; fill the look-ahead ring

    ; --- pre-roll [ap_preroll] halves straight into the grant ------------
    mov cx, [ap_preroll]
.pre:
    push cx
    call ap_stage_half            ; decode one half -> verb 6 -> ap_total += 2048
    pop cx
    loop .pre

    ; --- open the ring stream (verb 0) ---------------------------------
    mov al, 0
    mov ah, SND_OPENF_RING
    mov si, [ap_grant]
    mov cx, [ap_total]
    mov dx, [ap_rate]
    call OSAPI_SND_STREAM         ; out AL = 0 with AH = handle, else err
    or al, al
    jnz .snderr
    mov [ap_hand], ah
    mov byte [ap_sopen], 1

    ; --- start the worker (once) --------------------------------------
    cmp byte [ap_have_worker], 0
    jne .running
    mov ax, ap_worker
    mov bx, [ap_win]
    call OSAPI_TASK_SPAWN         ; CF = 1 refused (transient, SPEC.md 20.6)
    jc .notask                    ; no worker slot: say so and stop, rather than
    mov byte [ap_have_worker], 1  ; "Playing" over a ring nothing will feed
.running:
    mov byte [ap_state], AP_ST_PLAY
    mov word [ap_msg], ap_s_playing
    clc
    ret

.badfile:
    mov word [ap_msg], ap_s_loaderr
    jmp .refuse
.parseerr:
    mov ax, [apw_err]
    mov [ap_msg], ax
    jmp .refuse
.nomem:
    mov word [ap_msg], ap_s_nomem
    jmp .refuse
.notask:
    mov word [ap_msg], ap_s_notask
    jmp .snderr_msg               ; the stream is open: close it on the way out
.snderr:
    ; a verb-0 refusal above 22 kHz is the driver's rate gate: 24/32/44.1 kHz
    ; need an SB Pro (DSP >= 3.00) or an SB16 (SPEC.md 34.5/86.15). Name the
    ; card instead of the generic load error.
    mov word [ap_msg], ap_s_loaderr
    cmp word [ap_rate], 22050
    jbe .snderr_msg
    mov word [ap_msg], ap_s_hirate
.snderr_msg:
    call ap_stream_close
.refuse:
    stc
    ret

; =============================================================================
; ap_stage_half - decode one 2048-byte half and stage it into the SND ring
;                 grant at (ap_total & ap_rmask); advance ap_total by 2048.
;                 Silence-pads a short final half and sets ap_lasthalf.
; out: AL = 1 if this was the last half (EOF), else 0
; clobbers: nothing else (all pushed)
; =============================================================================
ap_stage_half:
    push bx
    push cx
    push dx
    push si
    push di
    ; The decoder is STATEFUL for ADPCM, and consuming a shared ring for PCM:
    ; a short apd_pull advances ap_la_rd and the predictor, and then .under
    ; discards the output - a permanent gap in the audio, heard as a stutter
    ; (SPEC.md 86.4 / 86.5.2). So decode a half only when a whole half's worth
    ; of INPUT is already in the look-ahead ring. At EOF the tail is
    ; silence-padded on purpose, so let that through.
    cmp byte [ap_src_done], 0
    jne .haveinput
    call ap_la_avail
    cmp ax, AP_HALF
    jb .under                    ; not enough yet - the SND ring has slack and
                                 ; ap_feed has already asked for a refill
.haveinput:
    mov cx, AP_HALF
    call apd_pull                 ; AX = PCM8 bytes produced into apd_out
    mov bx, ax
    xor dl, dl                    ; DL = "last half" flag
    cmp bx, AP_HALF
    jae .full
    ; short: is it EOF, or just a momentary underrun?
    call ap_src_eof
    jnc .under                    ; transient - do NOT stage a partial half
    ; EOF: pad the rest with silence (0x80) and mark this the last half
    mov di, apd_out
    add di, bx
    mov cx, AP_HALF
    sub cx, bx
    mov al, 0x80
    push es
    push ds
    pop es
    rep stosb
    pop es
    mov dl, 1
.full:
    ; stage apd_out -> grant + (ap_total & ap_rmask)
    mov di, [ap_total]
    and di, [ap_rmask]
    add di, [ap_grant]
    mov si, apd_out
    mov cx, AP_HALF
    mov al, 6                     ; verb 6: stage (kernel copies from our seg)
    call OSAPI_SND_STREAM
    APCNT apc_stage
    add word [ap_total], AP_HALF
    or dl, dl
    jz .notlast
    mov byte [ap_lasthalf], 1
.notlast:
    mov al, dl
    xor ah, ah
    inc ax                        ; dl=0 -> AX=1 (half), dl=1 -> AX=2 (final)
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret
.under:
    APCNT apc_pdisc
    xor ax, ax                    ; produced nothing usable this call
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; =============================================================================
; ap_calc_bpt - ap_bpt = rate / 18.2065  (bytes of PCM8 per system tick)
;               rate * 3600 / 65536 is that to 0.011%; take the product's high
;               word.  (The Tracker idiom, SPEC.md 45.15.)
; =============================================================================
ap_calc_bpt:
    push ax
    push dx
    mov ax, [ap_rate]
    mov dx, 3600
    mul dx                         ; DX:AX = rate * 3600
    mov [ap_bpt_lo], dx
    mov word [ap_bpt_hi], 0
    pop dx
    pop ax
    ret

; =============================================================================
; ap_calc_duration - ap_total_s from the data length and the format
;   PCM8   : seconds = datlen / rate
;   ADPCM  : samples = (datlen / balign) * spb ; seconds = samples / rate
; A rough value is fine; 0 means "unknown".
; =============================================================================
ap_calc_duration:
    push ax
    push bx
    push cx
    push dx
    mov word [ap_total_s], 0
    mov word [ap_total_pcm], 0
    mov word [ap_total_pcm+2], 0
    mov ax, [apw_datlen]
    mov dx, [apw_datlen+2]
    cmp byte [apw_fmt], AP_FMT_ADPCM
    jne .pcm
    ; samples = (datlen / balign) * spb  -- do it in 32/16
    mov bx, [apw_balign]
    or bx, bx
    jz .out
    cmp dx, bx
    jae .out                       ; would overflow div - leave duration unknown
    div bx                         ; AX = blocks ; ignore remainder
    mul word [apw_spb]             ; DX:AX = total samples
    jmp .bysamps
.pcm:
    ; samples = datlen (1 byte/sample)
.bysamps:
    mov [ap_total_pcm], ax         ; total PCM8 output bytes = sample count
    mov [ap_total_pcm+2], dx
    mov bx, [ap_rate]
    or bx, bx
    jz .out
    cmp dx, bx
    jae .out
    div bx                         ; AX = seconds
    mov [ap_total_s], ax
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; ap_stream_close - close the stream (verb 2, UI only), drain the worker's
;                   in-flight feed pass, hand the ring grant back.
; Every UI path that resets engine state calls this first (SPEC.md 45.2).
; =============================================================================
ap_stream_close:
    push ax
    cmp byte [ap_sopen], 0
    je .drain
    mov al, 2
    mov ah, [ap_hand]
    call OSAPI_SND_STREAM
    mov byte [ap_sopen], 0
.drain:
    cmp byte [ap_mixing], 0
    jne .drain                     ; bounded: the worker re-checks ap_sopen
    call ap_free_ring
    mov byte [ap_ended], 0
    mov byte [ap_lasthalf], 0
    pop ax
    ret

; =============================================================================
; ap_reap - if the worker latched "track ended", close it and move on.
;           Called from ap_onwake and from the paint/click callbacks.
; =============================================================================
ap_reap:
    cmp byte [ap_ended], 0
    je .out
    call ap_autoadvance
.out:
    ret

; =============================================================================
; ap_autoadvance - a track finished: step the playlist and start the next, or
;                  stop at the end of the list.
; =============================================================================
ap_autoadvance:
    call ap_stream_close
    mov al, 1
    call apl_advance              ; CF = 1 => list ended, Repeat off
    jc .end
    call ap_open_track
    ret
.end:
    mov byte [ap_state], AP_ST_STOP
    mov word [ap_elapsed_s], 0
    mov word [ap_msg], ap_s_endlist
    ret

; =============================================================================
; transport commands (called from buttons / menu / keys)
; =============================================================================
ap_cmd_play:
    cmp byte [ap_state], AP_ST_PAUSE
    jne .fresh
    mov byte [ap_paused], 0
    mov byte [ap_state], AP_ST_PLAY
    mov word [ap_msg], ap_s_playing
    ret
.fresh:
    call ap_open_track
    ret

ap_cmd_pause:
    cmp byte [ap_state], AP_ST_PLAY
    jne .out
    mov byte [ap_paused], 1
    mov byte [ap_state], AP_ST_PAUSE
    mov word [ap_msg], ap_s_paused
.out:
    ret

ap_cmd_stop:
    call ap_stream_close
    mov byte [ap_state], AP_ST_STOP
    mov word [ap_elapsed_s], 0
    mov word [ap_msg], ap_s_stopped
    ret

ap_cmd_next:
    call ap_stream_close
    mov al, 1
    call apl_advance
    jc .end
    call ap_open_track
    ret
.end:
    mov word [ap_msg], ap_s_endlist
    call ap_cmd_stop
    ret

ap_cmd_prev:
    call ap_stream_close
    mov al, 0xFF
    call apl_advance
    jc .end
    call ap_open_track
    ret
.end:
    ret

ap_cmd_shuffle:
    mov al, [ap_shuffle]
    xor al, 1
    call apl_set_shuffle
    ret

ap_cmd_repeat:
    mov al, [ap_repeat]
    xor al, 1
    mov [ap_repeat], al
    ret

; =============================================================================
; ap_has_wav_ext - SI -> NUL name.  ZF = 1 iff it ends ".WAV" (any case).
; preserves every register except flags.
; =============================================================================
ap_has_wav_ext:
    push ax
    push cx
    push si
    xor cx, cx
.cl:
    cmp byte [si], 0
    je .got
    inc si
    inc cx
    cmp cx, 64
    jb .cl
.got:
    cmp cx, 4
    jb .ne
    sub si, 4                      ; SI back to the '.' (SI is at the NUL now)
    mov al, [si]
    cmp al, '.'
    jne .ne
    mov al, [si+1]
    call ap_upcase
    cmp al, 'W'
    jne .ne
    mov al, [si+2]
    call ap_upcase
    cmp al, 'A'
    jne .ne
    mov al, [si+3]
    call ap_upcase
    cmp al, 'V'
    jne .ne
    xor al, al                    ; ZF = 1
    jmp .done
.ne:
    or al, 1                       ; ZF = 0
.done:
    pop si
    pop cx
    pop ax
    ret

; =============================================================================
; ap_add_file - the ONE way a track enters the playlist (SPEC.md 86.2.1).
; Open, drag/drop (n/a), association and the startup ARG_FILE all converge here.
; in:  SI -> NUL 8.3 name (this segment), DX = dir cluster, BL = volume,
;      AL = AP_ADD_* flags
; out: CF = 0 accepted (ap_msg set), CF = 1 refused (ap_msg set to the reason)
; clobbers: AX..DI (UI-task callers)
; =============================================================================
ap_add_file:
    mov [ap_af_flags], al
    mov [ap_af_dir], dx
    mov [ap_af_vol], bl
    mov [ap_af_name], si

    call ap_has_wav_ext
    jne .notwav

    ; duplicate? same name + dir + vol -> select it, do not add again
    mov si, [ap_af_name]
    mov dx, [ap_af_dir]
    mov bl, [ap_af_vol]
    call apl_find                  ; out AX = index, CF = 1 not found
    jc .fresh
    mov word [ap_msg], ap_s_dup
    test byte [ap_af_flags], AP_ADD_PLAY
    jz .selonly
    ; already in the list: restart it ONLY if it is not the track already
    ; playing. A re-poll of APQUEUE.DAT that keeps handing us the same file
    ; must NOT keep tearing the stream down (SPEC.md 86.11.1).
    cmp al, [apl_cur]
    jne .selplay
    cmp byte [ap_state], AP_ST_PLAY
    je .ok                         ; same track, already playing - leave it
.selplay:
    mov [apl_cur], al
    jmp .startcur
.selonly:
    mov [apl_cur], al
    jmp .ok
.fresh:
    mov si, [ap_af_name]
    mov dx, [ap_af_dir]
    mov bl, [ap_af_vol]
    call apl_add                   ; CF = 1 list full
    jc .full
    mov al, [apl_n]
    dec al                         ; index of the entry just added
    mov ah, al                     ; keep it
    test byte [ap_af_flags], AP_ADD_CUR | AP_ADD_PLAY
    jz .notcur
    mov [apl_cur], al
.notcur:
    mov word [ap_msg], ap_s_added

    test byte [ap_af_flags], AP_ADD_PLAY
    jnz .startcur
    test byte [ap_af_flags], AP_ADD_IDLE
    jz .ok
    cmp byte [ap_state], AP_ST_PLAY
    je .ok                         ; Open while playing: added, not interrupted
    mov [apl_cur], ah
.startcur:
    call ap_stream_close
    call ap_open_track             ; sets ap_msg itself
.ok:
    clc
    ret
.notwav:
    mov word [ap_msg], ap_s_notwav
    stc
    ret
.full:
    mov word [ap_msg], ap_s_full
    stc
    ret

    APW ap_af_name
    APW ap_af_dir
    APB ap_af_vol
    APB ap_af_flags

; =============================================================================
; ap_find_sibling - CF = 0 iff another live "AUDIO PLAYER" instance exists in a
;                   different region segment (SPEC.md 86.11.1). Entry-proc only.
; clobbers: AX..DI
; =============================================================================
ap_pkgname: db 'AUDIO PLAYER', 0

ap_find_sibling:
    push ds
    pop es
    mov di, ap_snap
    call OSAPI_SYS_SNAPSHOT        ; AX = MAX_TASKS, BX = INST_MAX
    mov cx, INST_MAX
    mov si, ap_snap + SS_INST
    mov bx, cs                     ; our own region segment
.rec:
    cmp byte [si+SSI_STATE], 1
    jne .next
    test byte [si+SSI_KIND], KIND_PKG
    jz .next
    cmp [si+SSI_SEG], bx
    je .next                       ; that record is us
    lea di, [si+SSI_NAME]
    push si
    mov si, di
    mov di, ap_pkgname
    call ap_nameeq                 ; ZF = 1 equal
    pop si
    je .found
.next:
    add si, SSI_RECSZ
    loop .rec
    stc
    ret
.found:
    clc
    ret

; =============================================================================
; ap_que_root - move this instance to the system volume's root (where
;               APQUEUE.DAT lives).  clobbers AX, BX, DX.  CF = 1 on failure.
;
; OSAPI_FILE_GOTO, not its quiet twin: GOTO_Q sets only a transient by-cluster
; resolution dir and OSAPI_FILE_WRITE / _READ by NAME still land in the
; instance's own launched-from folder (measured - APDIAG.TXT and the queue file
; both missed the root). The real GOTO moves the instance, so every caller
; banks OSAPI_FILE_HERE first and GOTOs back (the FONTS/ pattern, os88type.inc).
; =============================================================================
ap_que_root:
    call OSAPI_VOL_SYS             ; BL = the volume this machine booted from
    xor dx, dx
    call OSAPI_FILE_GOTO          ; DX = 0 (root), BL = volume; REMOUNT
    ret

; =============================================================================
; ap_que_write - append the banked ARG_FILE document to APQUEUE.DAT on the
;                system volume root (SPEC.md 86.11.1).  Entry-proc only.
;
; READ-MODIFY-WRITE, not OSAPI_FILE_APPEND: the queue grows one 16-byte record
; at a time and APPEND needs the file's size to be a whole number of clusters
; (it never is here) AND needs the file to already exist (the first handoff's
; would not). The queue is <= 128 bytes, so read it whole, add our record,
; write it whole.
;
; out: CF = 1 could not write (read-only volume) - caller falls back to a
;      normal launch (a second window).
; =============================================================================
ap_que_write:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call OSAPI_FILE_HERE           ; bank our launched-from folder
    mov [ap_que_bdir], dx
    mov [ap_que_bvol], bl

    call ap_que_root
    jc .fail

    ; --- read the existing queue (if any) into the front of the buffer -----
    push ds
    pop es
    mov si, ap_qfile
    mov bx, ap_querec
    mov cx, AP_QUEREC * 7          ; 112: room for our record after it
    xor dx, dx
    call OSAPI_FILE_READ           ; CF=0 -> DX:AX = bytes; CF=1 -> none yet
    jnc .haveold
    xor ax, ax                     ; no file -> start empty
.haveold:
    mov di, ap_querec
    add di, ax                     ; DI -> where our record goes
    push ax                        ; keep the old length

    ; --- stage our 16-byte record: 13 name (NUL-padded) + word dir + byte vol
    mov si, ap_arg_name
    xor cx, cx
.c:
    cmp cx, AP_QUENAME_LEN
    jae .cpad
    mov al, [si]
    mov [di], al
    or al, al
    jz .cpad
    inc si
    inc di
    inc cx
    jmp .c
.cpad:
    cmp cx, AP_QUENAME_LEN
    jae .cdone
    mov byte [di], 0
    inc di
    inc cx
    jmp .cpad
.cdone:
    pop ax                         ; old length
    push ax
    mov di, ap_querec
    add di, ax
    mov dx, [ap_arg_dir]
    mov [di+13], dx
    mov dl, [ap_arg_vol]
    mov [di+15], dl

    pop ax                         ; old length
    add ax, AP_QUEREC              ; total to write
    mov cx, ax
    push ds
    pop es
    mov si, ap_qfile
    mov bx, ap_querec
    xor dx, dx
    call OSAPI_FILE_WRITE          ; whole-file: creates or overwrites
    pushf
    ; --- restore our folder regardless -----------------------------------
    mov dx, [ap_que_bdir]
    mov bl, [ap_que_bvol]
    call OSAPI_FILE_GOTO
    popf
    jc .fail
    clc
    jmp .out
.fail:
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; =============================================================================
; ap_que_poll - a live player checks APQUEUE.DAT for documents handed off by a
;               would-be second instance, adds them, and deletes the file.
;               Called from ap_onwake; throttled to AP_QUE_TICKS.  UI task.
; clobbers: AX..DI
; =============================================================================
; out: CF = 0 iff a document was added (the caller should repaint); CF = 1
;      otherwise (throttled, or the queue was empty).
; =============================================================================
ap_que_poll:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov byte [ap_que_hit], 0
    call OSAPI_GET_TICKS
    mov bx, ax
    sub ax, [ap_que_t]
    cmp ax, AP_QUE_TICKS
    jb .done
    mov [ap_que_t], bx
    cmp byte [ap_sopen], 0
    je .qfull
    cmp byte [ap_paused], 0
    jne .qfull
    call ap_la_avail
    cmp ax, AP_LA_LOW
    jb .done                      ; ring below low-water - wait
.qfull:
    APCNT apc_quechk

    ; GOTO_Q, not the real GOTO: this runs on every idle ap_onwake, and a real
    ; remount of the system root (scan + sort + icon harvest) is seconds on an
    ; XT. GOTO_Q only re-points name resolution and does not move us, so there
    ; is nothing to restore afterwards.
    call OSAPI_VOL_SYS            ; BL = the system volume
    xor dx, dx
    call OSAPI_FILE_GOTO_Q        ; resolve names at the root; cheap, no move
    push ds
    pop es
    mov si, ap_qfile
    mov bx, ap_querec
    mov cx, 128
    xor dx, dx
    call OSAPI_FILE_READ          ; out CF=0 DX:AX = bytes, else AX = FERR_*
    jc .qhome                     ; no queue file: nothing pending
    or ax, ax
    jz .qhome
    push ax                      ; keep the byte count
    mov si, ap_qfile
    call OSAPI_FILE_DELETE        ; still resolving at the root
    pop cx
    ; re-point name resolution back to our own folder before ap_add_file:
    ; OSAPI_FILE_HERE is unmoved by GOTO_Q, so it still names where we live.
    push cx
    call OSAPI_FILE_HERE
    call OSAPI_FILE_GOTO_Q
    pop cx

    mov si, ap_querec
.rec:
    cmp cx, AP_QUEREC
    jb .done
    push cx
    push si
    mov dx, [si+13]
    mov bl, [si+15]
    mov al, AP_ADD_CUR | AP_ADD_PLAY
    call ap_add_file
    mov byte [ap_que_hit], 1
    pop si
    pop cx
    add si, AP_QUEREC
    sub cx, AP_QUEREC
    jmp .rec
.qhome:
    call OSAPI_FILE_HERE
    call OSAPI_FILE_GOTO_Q        ; name resolution back to our folder
.done:
    cmp byte [ap_que_hit], 0
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    je .miss
    clc
    ret
.miss:
    stc
    ret

    APB  ap_que_hit
    APW  ap_que_bdir
    APB  ap_que_bvol
