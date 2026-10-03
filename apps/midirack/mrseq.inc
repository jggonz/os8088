; =============================================================================
; os8088 - apps/midirack/mrseq.inc
;
; THE SEQUENCER (SPEC.md 105.5): a Standard MIDI File held whole in one heap
; claim, its tracks merged by their next tick, and every event handed to the
; output in play order. Prefix mrq_.
;
; TIME IS THE OUTPUT'S. The sequencer counts in UNITS, and the output says how
; many there are in a second ([mrq_urate]): the synth's are its own samples,
; so an event lands on the sample it is due at, and the FM output's are PIT
; counts / 64 (18,643 a second), so an event lands within 54 us of its time.
; A tempo is turned into units per tick ONCE, when it changes (mrq_settempo);
; every event after that is one 32 x 32 multiply-add (mrq_madd).
;
;   mrq_load     the file's header and track table, from the claim
;   mrq_rewind   every track back to its first event, the clock to zero
;   mrq_due      DX:AX = the units the next event is due at; CF = 1 the end
;   mrq_step     the clock to the next event's tick, and every event there
;
; THE FILE IS HOSTILE (SPEC.md 105.5.1): every byte is read through
; mrq_getb, which refuses past the track's own end, and a track that runs off
; its end, has data where a status belongs, or a VLQ longer than four bytes
; is simply ended. Nothing a file says can make the sequencer read outside
; its claim or loop without consuming a byte.
; =============================================================================

MRQ_MAXTRK  equ 24                  ; tracks taken; more are ignored, said so

    MRB mrq_ntrk                    ; tracks in the table
    MRW mrq_div                     ; ticks a quarter
    MRB mrq_fmt                     ; 0 / 1
    MRB mrq_ntrkf                   ; tracks the FILE declared (for MIDI Info)
    MRBUF mrq_pos, MRQ_MAXTRK * 2   ; per track: next byte...
    MRBUF mrq_end, MRQ_MAXTRK * 2   ; ...one past its last
    MRBUF mrq_beg, MRQ_MAXTRK * 2   ; ...its first (rewind)
    MRBUF mrq_tlo, MRQ_MAXTRK * 2   ; ...the tick of its next event
    MRBUF mrq_thi, MRQ_MAXTRK * 2
    MRBUF mrq_rs, MRQ_MAXTRK        ; ...running status
    MRBUF mrq_live, MRQ_MAXTRK      ; ...1 while it has events
    MRW mrq_ticklo                  ; the clock: the tick we are at...
    MRW mrq_tickhi
    MRW mrq_u0                      ; ...and the units, 32.16: fraction,
    MRW mrq_u1                      ; low, high
    MRW mrq_u2
    MRW mrq_uptf                    ; units a tick, 16.16
    MRW mrq_upti
    MRW mrq_urate                   ; units a second (the output's)
    MRW mrq_templo                  ; the tempo, us a quarter
    MRW mrq_temphi
    MRB mrq_tsnum                   ; the time signature
    MRB mrq_tsden                   ; ...as a power of two
    MRW mrq_tsblo                   ; the tick its measures count from
    MRW mrq_tsbhi
    MRW mrq_tsbm                    ; ...and the measure that tick is
    MRB mrq_ended                   ; every track has run out
    MRB mrq_dvalid                  ; mrq_duelo/hi are current
    MRW mrq_duelo
    MRW mrq_duehi
    MRB mrq_evtrk                   ; the track mrq_step found
    MRBUF mrq_acc, 8                ; mrq_madd's 64-bit product
    MRW mrq_vt                      ; the output's table (MRO_*)

; the output's verbs: a table of near procs, one per row. The sequencer calls
; through it and knows nothing else about the output (SPEC.md 105.5.3)
MRO_ON      equ 0                   ; AL = ch, AH = key, CL = velocity
MRO_OFF     equ 2                   ; AL = ch, AH = key
MRO_PROG    equ 4                   ; AL = ch (the program is in mrc_prog)
MRO_CTL     equ 6                   ; AL = ch: volume, expression or pan moved
MRO_BEND    equ 8                   ; AL = ch: the bend moved
MRO_SUSUP   equ 10                  ; AL = ch: the pedal came up
MRO_ALLOFF  equ 12                  ; AL = ch, or 0xFF every channel: silence

; -----------------------------------------------------------------------------
; mrq_load - the header and the track table, from [mr_fseg]:0, [mr_fsize]
; out: CF = 0 a file we can play; CF = 1 not one, AX -> the reason (a string)
; -----------------------------------------------------------------------------
mrq_load:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov es, [mr_fseg]
    mov di, [mr_fsize]
    xor si, si
    mov ax, mr_s_notmidi
    cmp di, 14
    jb .bad
    cmp word [es:0], 'MT'
    jne .bad
    cmp word [es:2], 'hd'
    jne .bad
    cmp word [es:4], 0
    jne .bad
    mov bx, [es:6]                  ; the header's length, big-endian: 6
    xchg bl, bh
    cmp bx, 6
    jb .bad
    mov ax, [es:8]
    xchg al, ah
    mov cx, ax
    mov ax, mr_s_fmt2
    cmp cx, 1
    ja .bad                         ; format 2: independent songs, refused
    mov [mrq_fmt], cl
    mov ax, [es:10]
    xchg al, ah
    mov [mrq_ntrkf], al
    cmp ax, 255
    jbe .nt
    mov byte [mrq_ntrkf], 255
.nt:
    mov ax, [es:12]
    xchg al, ah
    mov cx, ax
    mov ax, mr_s_smpte
    or cx, cx
    jz .bad                         ; no ticks a quarter
    test ch, 0x80
    jnz .bad                        ; SMPTE time: refused, said so
    mov [mrq_div], cx
    add bx, 8                       ; BX = the first chunk
    mov si, bx
    mov byte [mrq_ntrk], 0
.chunk:
    mov ax, di                      ; a chunk header needs 8 bytes
    sub ax, si
    jc .done
    cmp ax, 8
    jb .done
    cmp byte [mrq_ntrk], MRQ_MAXTRK
    jae .done
    mov ax, [es:si+4]               ; the length: high word must be 0 (the
    or ax, ax                       ; file is at most a segment)
    jnz .clip
    mov cx, [es:si+6]
    xchg cl, ch                     ; CX = the chunk's length
    jmp short .len
.clip:
    mov cx, 0xFFFF
.len:
    lea bx, [si+8]                  ; BX = its body
    mov ax, di
    sub ax, bx
    cmp cx, ax
    jbe .fit
    mov cx, ax                      ; a chunk running past the file: clipped
.fit:
    cmp word [es:si], 'MT'
    jne .next
    cmp word [es:si+2], 'rk'
    jne .next
    push bx
    mov al, [mrq_ntrk]
    xor ah, ah
    shl ax, 1
    mov dx, bx                      ; DX = the body
    mov bx, ax
    mov [mrq_beg+bx], dx
    add dx, cx
    mov [mrq_end+bx], dx
    pop bx
    inc byte [mrq_ntrk]
.next:
    add bx, cx
    jc .done
    mov si, bx
    jmp .chunk
.done:
    mov ax, mr_s_notrk
    cmp byte [mrq_ntrk], 0
    je .bad
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
    pop bx
    ret

; -----------------------------------------------------------------------------
; mrq_rewind - every track to its first event, the clock to zero, the tempo
; and the time signature to the file's defaults (120 BPM, 4/4). The channel
; state is the output's to reset (mr_chan_reset).
; -----------------------------------------------------------------------------
mrq_rewind:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    xor ax, ax
    mov [mrq_ticklo], ax
    mov [mrq_tickhi], ax
    mov [mrq_u0], ax
    mov [mrq_u1], ax
    mov [mrq_u2], ax
    mov [mrq_tsblo], ax
    mov [mrq_tsbhi], ax
    mov [mrq_tsbm], ax
    mov byte [mrq_ended], 0
    mov byte [mrq_dvalid], 0
    mov byte [mrq_tsnum], 4
    mov byte [mrq_tsden], 2
    mov word [mrq_templo], 0xA120   ; 500,000 us: 120 BPM
    mov word [mrq_temphi], 0x0007
    call mrq_settempo
    mov es, [mr_fseg]
    xor bx, bx
.t:
    mov al, bl
    cmp al, [mrq_ntrk]
    jae .done
    mov byte [mrq_rs+bx], 0
    mov byte [mrq_live+bx], 1
    shl bx, 1
    mov si, [mrq_beg+bx]
    mov di, [mrq_end+bx]
    mov word [mrq_tlo+bx], 0
    mov word [mrq_thi+bx], 0
    shr bx, 1
    call mrq_delta                  ; the first event's tick
    inc bx
    jmp short .t
.done:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mrq_delta - read a delta into track BX's next tick, and park its cursor
; in:  BX = the track, ES:SI = its cursor, DI = its end
; out: [mrq_pos], [mrq_tlo/thi] advanced; the track ended if it ran out
; -----------------------------------------------------------------------------
mrq_delta:
    push ax
    push dx
    push bx
    call mrq_vlq                    ; DX:AX = the delta
    jc .dead
    shl bx, 1
    add [mrq_tlo+bx], ax
    adc [mrq_thi+bx], dx
    mov [mrq_pos+bx], si
    pop bx
    pop dx
    pop ax
    ret
.dead:
    pop bx
    mov byte [mrq_live+bx], 0
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mrq_getb - AL = the next byte of a track. in: ES:SI, DI = the end
; out: CF = 1 there is none (SI unmoved)
; -----------------------------------------------------------------------------
mrq_getb:
    cmp si, di
    jae .none
    mov al, [es:si]
    inc si
    clc
    ret
.none:
    stc
    ret

; mrq_vlq - a variable-length quantity: DX:AX, at most four bytes (28 bits)
; out: CF = 1 it ran out or was longer than four
mrq_vlq:
    push bx
    push cx
    xor dx, dx
    xor bx, bx                      ; DX:BX = the value
    mov cx, 4
.b:
    call mrq_getb
    jc .bad
    push ax
    push cx
    mov cx, 7                       ; DX:BX <<= 7
.sh:
    shl bx, 1
    rcl dx, 1
    loop .sh
    pop cx
    pop ax
    mov ah, al
    and al, 0x7F
    or bl, al
    test ah, 0x80
    jz .ok
    loop .b
.bad:
    pop cx
    pop bx
    stc
    ret
.ok:
    mov ax, bx
    pop cx
    pop bx
    clc
    ret

; -----------------------------------------------------------------------------
; mrq_settempo - units a tick from the tempo and the output's rate, 16.16:
;   upt = tempo_us x urate x 65536 / (div x 1,000,000)
;       = ((tempo_us x urate) / div) x 4096 / 62500
; done in three chained multi-word steps (mr_mul32x16, mr_mdiv), because
; the middle value is up to 39 bits. Clamped to a 32-bit answer.
; -----------------------------------------------------------------------------
mrq_settempo:
    push ax
    push bx
    push cx
    push dx
    push si
    mov ax, [mrq_templo]
    mov dx, [mrq_temphi]
    mov bx, [mrq_urate]
    call mr_mul32x16                ; [mr_w] = tempo x urate (48 bits)
    mov bx, [mrq_div]
    mov cx, 3
    call mr_mdiv                    ; ...divided by div
    mov si, mr_w                    ; x 4096: shifted left 12 into 64 bits
    mov word [mr_w+6], 0
    mov cx, 12
.sh:
    shl word [mr_w], 1
    rcl word [mr_w+2], 1
    rcl word [mr_w+4], 1
    rcl word [mr_w+6], 1
    loop .sh
    mov bx, 62500
    mov cx, 4
    call mr_mdiv
    cmp word [mr_w+4], 0
    jne .clamp
    cmp word [mr_w+6], 0
    jne .clamp
    mov ax, [mr_w]
    mov [mrq_uptf], ax
    mov ax, [mr_w+2]
    mov [mrq_upti], ax
    jmp short .out
.clamp:
    mov word [mrq_uptf], 0xFFFF
    mov word [mrq_upti], 0xFFFF
.out:
    mov byte [mrq_dvalid], 0
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mr_mul32x16 - [mr_w] (3 words) = DX:AX x BX
; mr_mdiv     - [mr_w] (CX words, little-endian) /= BX, in place; DX = the
;               remainder. A word chain of 8086 `div`s, most significant first
; -----------------------------------------------------------------------------
mr_mul32x16:
    push ax
    push cx
    push dx
    mov cx, dx                      ; CX = the high word
    mul bx
    mov [mr_w], ax
    mov [mr_w+2], dx
    mov ax, cx
    mul bx
    add [mr_w+2], ax
    adc dx, 0
    mov [mr_w+4], dx
    pop dx
    pop cx
    pop ax
    ret

mr_mdiv:
    push ax
    push cx
    push si
    or bx, bx
    jz .out                         ; (a zero divisor leaves it alone)
    mov si, cx
    dec si
    shl si, 1
    add si, mr_w                    ; SI = the most significant word
    xor dx, dx
.w:
    mov ax, [si]
    div bx                          ; DX < BX always, so this cannot trap
    mov [si], ax
    sub si, 2
    loop .w
.out:
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mrq_madd - [mrq_u] += DX:AX ticks x upt (16.16). The product is 64 bits
; in mrq_acc and the units keep 48 (32.16); the top is dropped, which is
; four thousand hours of song
; -----------------------------------------------------------------------------
mrq_madd:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, ax                      ; SI = ticks low, CX = ticks high
    mov cx, dx
    mov ax, si
    mul word [mrq_uptf]             ; lo x f
    mov [mrq_acc], ax
    mov [mrq_acc+2], dx
    xor ax, ax
    mov [mrq_acc+4], ax
    mov [mrq_acc+6], ax
    mov ax, si
    mul word [mrq_upti]             ; lo x i, at 2^16
    add [mrq_acc+2], ax
    adc [mrq_acc+4], dx
    adc word [mrq_acc+6], 0
    jcxz .sum                       ; (the common case: a small delta)
    mov ax, cx
    mul word [mrq_uptf]             ; hi x f, at 2^16
    add [mrq_acc+2], ax
    adc [mrq_acc+4], dx
    adc word [mrq_acc+6], 0
    mov ax, cx
    mul word [mrq_upti]             ; hi x i, at 2^32
    add [mrq_acc+4], ax
    adc [mrq_acc+6], dx
.sum:
    mov ax, [mrq_acc]
    add [mrq_u0], ax
    mov ax, [mrq_acc+2]
    adc [mrq_u1], ax
    mov ax, [mrq_acc+4]
    adc [mrq_u2], ax
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mrq_next - which track has the soonest event?
; out: CF = 0, BX = the track, DX:AX = its tick; CF = 1 none (the end)
; -----------------------------------------------------------------------------
mrq_next:
    push cx
    push si
    push di
    mov bx, 0xFFFF                  ; BX = the best so far
    xor si, si
.t:
    mov cx, si
    cmp cl, [mrq_ntrk]
    jae .done
    cmp byte [mrq_live+si], 0
    je .n
    push si
    shl si, 1
    mov cx, [mrq_thi+si]
    mov di, [mrq_tlo+si]
    pop si
    cmp bx, 0xFFFF
    je .take
    cmp cx, dx
    jb .take
    ja .n
    cmp di, ax
    jae .n
.take:
    mov bx, si
    mov dx, cx
    mov ax, di
.n:
    inc si
    jmp short .t
.done:
    cmp bx, 0xFFFF
    je .none
    pop di
    pop si
    pop cx
    clc
    ret
.none:
    pop di
    pop si
    pop cx
    stc
    ret

; -----------------------------------------------------------------------------
; mrq_due - DX:AX = the units the next event is due at; CF = 1 the song has
; ended. Cached until the clock moves (mrq_step) or the tempo does
; -----------------------------------------------------------------------------
mrq_due:
    cmp byte [mrq_ended], 0
    jne .end
    cmp byte [mrq_dvalid], 0
    je .calc
    mov ax, [mrq_duelo]
    mov dx, [mrq_duehi]
    clc
    ret
.calc:
    push bx
    call mrq_next
    jc .gone
    sub ax, [mrq_ticklo]            ; ticks from here
    sbb dx, [mrq_tickhi]
    push word [mrq_u0]              ; the units it lands at, without moving
    push word [mrq_u1]              ; the clock: madd, read, put back
    push word [mrq_u2]
    call mrq_madd
    mov ax, [mrq_u1]
    mov dx, [mrq_u2]
    pop word [mrq_u2]
    pop word [mrq_u1]
    pop word [mrq_u0]
    mov [mrq_duelo], ax
    mov [mrq_duehi], dx
    mov byte [mrq_dvalid], 1
    pop bx
    clc
    ret
.gone:
    mov byte [mrq_ended], 1
    pop bx
.end:
    stc
    ret

; -----------------------------------------------------------------------------
; mrq_step - the clock to the next event's tick, and EVERY event there, track
; by track in file order (so a tempo in track 0 is heard before the notes
; that share its tick). out: CF = 1 the song has ended
; -----------------------------------------------------------------------------
mrq_step:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call mrq_next
    jc .end
    push ax
    push dx
    sub ax, [mrq_ticklo]
    sbb dx, [mrq_tickhi]
    call mrq_madd                   ; the units, to that tick
    pop dx
    pop ax
    mov [mrq_ticklo], ax
    mov [mrq_tickhi], dx
    mov byte [mrq_dvalid], 0
    mov es, [mr_fseg]
    xor bx, bx
.t:
    mov cl, bl
    cmp cl, [mrq_ntrk]
    jae .done
.again:
    cmp byte [mrq_live+bx], 0
    je .n
    push bx
    shl bx, 1
    mov cx, [mrq_tlo+bx]
    mov dx, [mrq_thi+bx]
    pop bx
    cmp cx, [mrq_ticklo]
    jne .n
    cmp dx, [mrq_tickhi]
    jne .n
    call mrq_event                  ; one event, and the next delta
    jmp short .again
.n:
    inc bx
    jmp short .t
.done:
    clc
    jmp short .out
.end:
    mov byte [mrq_ended], 1
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mrq_event - one event of track BX, then its next delta. ES = the file.
; -----------------------------------------------------------------------------
mrq_event:
    push ax
    push cx
    push dx
    push si
    push di
    push bx
    shl bx, 1
    mov si, [mrq_pos+bx]
    mov di, [mrq_end+bx]
    pop bx
    call mrq_getb
    jc .dead
    test al, 0x80
    jnz .status
    mov ah, [mrq_rs+bx]             ; a data byte: running status
    or ah, ah
    jz .dead                        ; ...with none to run: a broken track
    dec si                          ; the byte is data: read it again below
    mov al, ah
    jmp short .chan
.status:
    cmp al, 0xF0
    jae .system
    mov [mrq_rs+bx], al
.chan:
    mov cl, al                      ; CL = the status
    call mrq_getb
    jc .dead
    mov ch, al                      ; CH = the first data byte
    mov al, cl
    and al, 0xF0
    cmp al, 0xC0
    je .one
    cmp al, 0xD0
    je .one
    call mrq_getb                   ; DL = the second
    jc .dead
    mov dl, al
    jmp short .disp
.one:
    xor dl, dl
.disp:
    and ch, 0x7F
    and dl, 0x7F
    call mr_chan_event              ; CL = status, CH = d1, DL = d2
    jmp short .next
.system:
    cmp al, 0xFF
    je .meta
    cmp al, 0xF0                    ; SysEx (F0 or F7): a length and bytes
    je .sysex
    cmp al, 0xF7
    jne .dead                       ; (F1..FE are not file events)
.sysex:
    mov byte [mrq_rs+bx], 0         ; SysEx cancels running status
    mov cl, al                      ; (F0 or F7, for MIDI out)
    push cx
    call mrq_vlq
    pop cx
    jc .dead
    cmp byte [mr_out], MRO_MIDI     ; MIDI OUT: forwarded, as the module is
    jne .sxskip                     ; owed (105.8.5) - nothing else here
    cmp byte [mr_dry], 0            ; plays SysEx
    jne .sxskip
    or dx, dx
    jnz .sxskip
    push cx                         ; (ES:SI, AX bytes - within the track:
    mov cx, di                      ; the skip below refuses one that is not)
    sub cx, si
    cmp ax, cx
    pop cx
    ja .sxskip
    call mrm_sysex
.sxskip:
    call mrq_skip
    jc .dead
    jmp short .next
.meta:
    mov byte [mrq_rs+bx], 0         ; a meta event cancels running status too
    call mrq_getb                   ; the type
    jc .dead
    mov cl, al
    push cx
    call mrq_vlq                    ; the length
    pop cx
    jc .dead
    or dx, dx
    jnz .dead                       ; a meta event over 64KB: not in this file
    cmp cl, 0x2F
    je .dead                        ; End of Track
    call mr_meta                    ; CL = type, AX = length, ES:SI = data
    call mrq_skip                   ; past it, whatever mr_meta did
    jc .dead
.next:
    call mrq_delta
    jmp short .out
.dead:
    mov byte [mrq_live+bx], 0
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; mrq_skip - SI += AX within the track; CF = 1 it runs past the end
mrq_skip:
    push cx
    mov cx, di
    sub cx, si
    cmp ax, cx
    ja .past
    add si, ax
    pop cx
    clc
    ret
.past:
    mov si, di
    pop cx
    stc
    ret

; -----------------------------------------------------------------------------
; mr_meta - the meta events the player reads. CL = the type, AX = the length,
; ES:SI = the data (all of it inside the track: the caller skips it after)
; -----------------------------------------------------------------------------
mr_meta:
    push ax
    push bx
    push cx
    push dx
    cmp cl, 0x51
    je .tempo
    cmp cl, 0x58
    je .tsig
    cmp cl, 0x01                    ; text, copyright, track name: the first
    jb .out                         ; one the song has is MIDI Info's line
    cmp cl, 0x03
    ja .out
    cmp byte [mr_dry], 0
    je .out                         ; (only the load's dry run collects it)
    cmp byte [mr_text], 0
    jne .out
    call mr_text_take
    jmp short .out
.tempo:
    cmp ax, 3
    jb .out
    mov dl, [es:si]                 ; 24 bits, big-endian
    xor dh, dh
    mov ah, [es:si+1]
    mov al, [es:si+2]
    or dx, dx
    jnz .tset
    or ax, ax
    jz .out                         ; a tempo of 0: refused, the old one stands
.tset:
    mov [mrq_templo], ax
    mov [mrq_temphi], dx
    call mrq_settempo
    jmp short .out
.tsig:
    cmp ax, 2
    jb .out
    call mrq_meas                   ; the measures up to here, under the old
    mov [mrq_tsbm], ax              ; signature, become the base
    mov ax, [mrq_ticklo]
    mov [mrq_tsblo], ax
    mov ax, [mrq_tickhi]
    mov [mrq_tsbhi], ax
    mov al, [es:si]
    or al, al
    jz .out
    mov [mrq_tsnum], al
    mov al, [es:si+1]
    cmp al, 6
    jbe .den
    mov al, 6
.den:
    mov [mrq_tsden], al
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mrq_meas - AX = the measure the clock is in, 0-based
;   base + (tick - tsbase) / (div x 4 x num >> den)
; -----------------------------------------------------------------------------
mrq_meas:
    push bx
    push cx
    push dx
    mov ax, [mrq_div]
    shl ax, 1
    shl ax, 1                       ; ticks a whole note (div < 16384)
    jc .one
    mov cl, [mrq_tsden]
    shr ax, cl                      ; ...a beat of the signature
    or ax, ax
    jz .one
    mov bl, [mrq_tsnum]
    xor bh, bh
    mul bx                          ; DX:AX = ticks a measure
    mov bx, ax
    mov cx, dx                      ; CX:BX
    mov ax, [mrq_ticklo]
    mov dx, [mrq_tickhi]
    sub ax, [mrq_tsblo]
    sbb dx, [mrq_tsbhi]
    call mr_div32                   ; DX:AX = measures since
    add ax, [mrq_tsbm]
    jmp short .out
.one:
    mov ax, [mrq_tsbm]
.out:
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; mr_div32 - DX:AX /= CX:BX (unsigned, shift and subtract); a zero divisor
; answers 0. Rare (a repaint's measure), so a loop and not cleverness
; -----------------------------------------------------------------------------
mr_div32:
    push bx
    push cx
    push si
    push di
    push bp
    mov bp, cx
    or bp, bx
    jz .zero
    xor si, si                      ; SI:DI = the remainder
    xor di, di
    mov bp, 32
.b:
    shl ax, 1
    rcl dx, 1
    rcl di, 1
    rcl si, 1
    cmp si, cx
    jb .n
    ja .sub
    cmp di, bx
    jb .n
.sub:
    sub di, bx
    sbb si, cx
    inc ax                          ; the bit just shifted in
.n:
    dec bp
    jnz .b
    jmp short .out
.zero:
    xor ax, ax
    xor dx, dx
.out:
    pop bp
    pop di
    pop si
    pop cx
    pop bx
    ret
