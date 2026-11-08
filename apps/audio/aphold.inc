; =============================================================================
; os8088 - apps/audio/aphold.inc - THE HOLD, kern_mega's arm (SPEC.md 86.5.4)
;
; %included by audio.asm under PKG_MEGA (apps/os88mega.inc): the shipped
; build carries none of it. A WAV the machine can hold is played FROM MEMORY:
; before the track opens, the WHOLE FILE is read into ONE OSAPI_MEM_HUGE
; claim - a window every 64KB, a bridge of the pool and the heap allowed
; (SPEC.md 50.3.5) - a time-boxed run of 32 KB reads each ap_onwake pass,
; the status line saying 'Loading 42%'; from then on every gulp of the
; look-ahead (ap_refill_chunk's AP_RSEQ, aph_rseq) is a COPY out of the hold,
; and the play reads no disk. That retires KERN-MEGA-PLAN 14.156's concern
; (a floppy refill losing a revolution a call while the card plays alone) for
; every file that fits; a file that does not - or a load that fails, or
; [aph_off] set - streams through the 14.160 look-ahead exactly as before.
; XTMP3's hold (apps/xtmp3/x3hold.inc, SPEC.md 121.6.1) is the precedent, its
; walker apps/os88huge.inc (SPEC.md 50.3.8) the way in and out: every read
; and every copy is split where a window ends, so never across a bridge's cut.
;
; THE KEY is the file: its folder, volume and name. A track that opens with
; the same key keeps the hold (Repeat on a one-track list, Previous back to
; it); any other frees it first, and so do Stop, Clear playlist and a load
; that fails. The kernel frees the claim with the instance.
; =============================================================================

APH_CHUNK   equ 32768               ; one read: a cluster multiple on any disk
APH_MARGKB  equ 96                  ; the heap left free beside the hold (XTMP3's
                                    ; X3H_MARGKB): the card's grant, the speaker
                                    ; ring and a program launched while it plays
APH_PASS    equ 2                   ; a pass's load: reads until two ticks
                                    ; (~110 ms) have gone

%define HW_CLAIM                    ; apps/os88huge.inc: the walker
%define HW_GET
%define HW_RSEQ
%include "os88huge.inc"

    APBUF aph_hw, HW_SIZE           ; the hold's walker record (HW_LEN: KB x 1K)
    APW   aph_base                  ; the claim, 0 = none
    APB   aph_on                    ; 1 = the file is in, whole
    APB   aph_load                  ; 1 = it is loading (ap_onwake's passes)
    APB   aph_hcv                   ; 1 = conventional memory holds part of it
    APB   aph_skip                  ; 1 = the next open streams (a load failed)
    APB   aph_off                   ; 1 = never hold (a row's streamed case)
    APB   aph_pct                   ; the percentage the status line says
    APW   aph_t0                    ; a pass's first tick
    APW   aph_kb                    ; the hold's KB (a 32 KB multiple)
    APW   aph_skb                   ; ...and the file's, rounded up
    APD   aph_size                  ; the file's bytes
    APD   aph_have                  ; ...and how many are in
    APW   aph_dir                   ; THE KEY: the folder, the volume, the name
    APB   aph_vol
    APBUF aph_name, 13
    APBUF aph_cur, FSEQ_SIZE        ; the load's READ_SEQ cursor
    APW   aph_ncopy                 ; gulps copied out of the hold...
    APW   aph_ndisk                 ; ...and read off the disk (rows read both)
    APBUF aph_msg, 14               ; 'Loading 100%', NUL

; -----------------------------------------------------------------------------
; aph_begin - ap_open_track's first question (UI task): is the current entry's
; file held, or can it be? out: CF = 0 open the track now (held whole already,
; or it streams); CF = 1 a load is running (begun now, or this file's still
; going) - ap_onwake opens the track when it is in. ap_msg says 'Loading'.
; clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
aph_begin:
    cmp byte [aph_off], 0
    jne .now
    call apl_goto_cur               ; the entry's folder, its name, its size
    jc .now                         ; (ap_prep_track says what is wrong)
    call OSAPI_FILE_HERE            ; DX = the folder, BL = the volume
    push dx
    push bx
    call apl_cur_name               ; SI = the name
    pop bx
    pop dx
    jc .now
    call aph_same                   ; THIS file: held, or loading
    jnc .same
    call aph_free
    cmp byte [aph_skip], 0          ; its load failed a moment ago: stream it
    mov byte [aph_skip], 0
    jne .now
    mov [aph_dir], dx               ; the key
    mov [aph_vol], bl
    mov di, aph_name
    mov cx, 12
.cp:
    lodsb
    mov [di], al
    inc di
    or al, al
    loopnz .cp
    mov byte [di], 0
    mov si, aph_name
    call ap_filesize                ; DX:AX = its bytes
    jc .now
    mov [aph_size], ax
    mov [aph_size+2], dx
    add ax, 1023                    ; its KB, rounded up to 32 KB: each read
    adc dx, 0                       ; a whole APH_CHUNK capacity however the
    mov cx, 10                      ; file ends
.kb:
    shr dx, 1
    rcr ax, 1
    loop .kb
    or dx, dx
    jnz .now
    mov [aph_skb], ax               ; (the percentage's whole)
    add ax, 31
    jc .now
    and al, 0xE0
    or ax, ax
    jz .now
    push ax
    call ap_ensure_bufs             ; the look-ahead FIRST, so it keeps its
    pop ax                          ; page-safe pool block (KERN-MEGA-PLAN
    jc .now                         ; 14.160) and the hold takes what is left
    call aph_claim                  ; the hold, or nothing - the look-ahead
    pushf                           ; PINNED meanwhile, so the claim's pack
    call ap_rdecl                   ; leaves it on the pool's floor (14.166);
    popf                            ; no play is open: movable again
    jc .now
    xor ax, ax                      ; the load begins
    mov [aph_have], ax
    mov [aph_have+2], ax
    mov byte [aph_load], 1
    mov byte [aph_pct], 0xFF
    mov di, aph_cur
    call os88_rseq_new
    mov byte [ap_state], AP_ST_STOP ; nothing plays until it is in
    call aph_text                   ; 'Loading 0%' (the caller repaints: the
    mov bx, [ap_win]                ; gfx lock is not reentrant)
    call OSAPI_WM_WAKE              ; ap_onwake's first pass
.same:
    cmp byte [aph_load], 0          ; held whole: CF = 0, open it
    je .now
    mov word [ap_msg], aph_msg
    stc
    ret
.now:
    clc
    ret

; aph_same - DX = a folder, BL = its volume, SI -> a name: CF = 0 the hold
; (whole or loading) is that file's. keeps everything but flags
aph_same:
    cmp word [aph_base], 0
    je .no
    cmp dx, [aph_dir]
    jne .no
    cmp bl, [aph_vol]
    jne .no
    push ax
    push si
    push di
    mov di, aph_name
.c:
    mov al, [si]
    cmp al, [di]
    jne .ne
    inc si
    inc di
    or al, al
    jnz .c
    pop di
    pop si
    pop ax
    clc
    ret
.ne:
    pop di
    pop si
    pop ax
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; aph_claim - AX = KB: ONE OSAPI_MEM_HUGE claim with a window every 64KB, a
; bridge allowed, the heap keeping APH_MARGKB beside it. The kernel takes the
; pool alone, else the heap alone, else the bridge (XTMP3's x3h_huge): where
; the heap ALONE took it and the margin is short it is claimed once more past
; the heap's largest run - a bridge, its conventional piece the smaller by
; the pool's. The conventional part MOVABLE, a `ret` its proc (its windows
; follow a move), pinned for each read. A paragraph step - kern_mega in real
; mode - is held too: the walker is the same. The AX = 0 ask comes first, so
; a file too big costs no claim - and no pack of the pool - at all.
; out: CF = 0 [aph_base], aph_hw, [aph_kb], [aph_hcv]; CF = 1 none. keeps all
; -----------------------------------------------------------------------------
aph_claim:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov si, ax                      ; SI = the KB
    xor bp, bp                      ; BP = 1 on the second claim
    xor ax, ax                      ; THE ASK first (SPEC.md 50.3.5): nothing
    xor bx, bx                      ; claimed and nothing packed for a file
    call OSAPI_MEM_HUGE             ; the machine cannot hold - AX = the most
    jc .claim                       ; KB one claim is given now
    cmp ax, si
    jb .no
.claim:
    xor dx, dx
    mov bl, MEMP_AVAIL
    call OSAPI_MEM_P                ; BX = the heap's free paragraphs
    mov di, bx                      ; DI = ...before
    mov ax, si
    xor cx, cx
    mov bx, aph_hw
    call hw_claim                   ; DX = the claim
    jc .no
    push dx
    xor dx, dx
    mov bl, MEMP_AVAIL
    call OSAPI_MEM_P                ; AX = the largest run, BX = all, after
    pop dx
    mov byte [aph_hcv], 0
    cmp bx, di
    jae .m
    mov byte [aph_hcv], 1           ; the heap fell: it holds part of it
.m:
    cmp bx, APH_MARGKB * 64
    jae .ok
    cmp byte [aph_hcv], 0           ; SHORT: only where the heap alone took it
    je .back                        ; (no cut), and once
    cmp word [aph_hw+HW_CUT], 0
    jne .back
    or bp, bp
    jnz .back
    inc bp
    call OSAPI_MEM_FREE
    xor dx, dx
    mov bl, MEMP_AVAIL
    call OSAPI_MEM_P                ; AX = the heap's largest run, paragraphs
    mov cl, 6
    shr ax, cl
    add ax, 32                      ; ...past it, in 32 KB
    and al, 0xE0
    xchg si, ax
    jmp short .claim
.ok:
    mov [aph_base], dx
    mov [aph_kb], si
    call aph_decl
    clc
    jmp short .o
.back:
    call OSAPI_MEM_FREE
.no:
    stc
.o:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; aph_pin / aph_decl - the hold's conventional piece pinned (a read is about
; to land in it) / movable again. Nothing when the pool holds it all. keep all
aph_pin:
    push ax
    xor ax, ax
    jmp short aph_mset
aph_decl:
    push ax
    mov ax, ap_reap.out             ; (a bare `ret`)
aph_mset:
    cmp byte [aph_hcv], 0
    je .o
    push dx
    mov dx, [aph_base]
    call OSAPI_MEM_MOVABLE
    pop dx
.o:
    pop ax
    clc
    ret

; aph_free - the hold given back, loaded or loading. keeps everything
aph_free:
    push dx
    xor dx, dx
    mov byte [aph_on], dl
    mov byte [aph_load], dl
    xchg dx, [aph_base]
    or dx, dx
    jz .o
    call OSAPI_MEM_FREE
.o:
    pop dx
    ret

; -----------------------------------------------------------------------------
; aph_tick - ap_onwake's pass while [aph_load]: reads until APH_PASS ticks
; have gone or the file is in. In: the track opened (from memory now). A read
; that fails: the hold given up, and the track opened streaming. UI task, no
; gfx lock. clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
aph_tick:
    call OSAPI_GET_TICKS
    mov [aph_t0], ax
.rd:
    call aph_read1                  ; CF = 1 failed; ZF = 1 all in
    jc .fail
    jz .in
    call OSAPI_GET_TICKS
    sub ax, [aph_t0]
    cmp ax, APH_PASS
    jb .rd
    call aph_text                   ; 'Loading 42%', drawn when it moved:
    jnc .wake                       ; the status line alone (the figure only
    mov bx, [ap_win]                ; grows, and the open drew the window)
    call OSAPI_WM_OBSCURED
    jc .wake
    call OSAPI_GFX_LOCK
    call OSAPI_WM_CLIP_SET
    jc .un
    call apu_origin
    mov si, aph_msg
    mov dx, 16
    call apu_run6                   ; where apu_draw puts the status line
.un:
    call OSAPI_GFX_UNLOCK
.wake:
    mov bx, [ap_win]
    call OSAPI_WM_WAKE              ; ...and the next pass
    ret
.fail:
    call aph_free
    mov byte [aph_skip], 1          ; the open below streams it
    jmp short .open
.in:
    mov byte [aph_load], 0
    mov byte [aph_on], 1
.open:
    call OSAPI_GFX_LOCK
    call ap_open_track              ; (sets ap_msg itself)
    jmp ap_repunlock

; aph_read1 - one APH_CHUNK of the file into the hold, standing in its folder
; first (File > Open elsewhere moves us, SPEC.md 38.10). out: CF = 1 a read
; failed; CF = 0 and ZF = 1 the whole file is in, ZF = 0 more to come.
; clobbers AX, BX, CX, DX, SI, DI
aph_read1:
    mov ax, [aph_have]
    mov dx, [aph_have+2]
    cmp dx, [aph_size+2]
    jne .more
    cmp ax, [aph_size]
.more:
    jae .in
    push ax
    push dx
    call OSAPI_FILE_HERE
    cmp dx, [aph_dir]
    jne .go
    cmp bl, [aph_vol]
    je .here
.go:
    mov dx, [aph_dir]
    mov bl, [aph_vol]
    call OSAPI_FILE_GOTO_QM
.here:
    pop dx
    pop ax
    jc .bad
    mov cx, APH_CHUNK
    mov si, aph_name
    mov di, aph_cur
    mov bx, aph_hw
    call aph_pin
    call hw_fread                   ; DX:AX = delivered (no more than CX)
    call aph_decl                   ; (keeps the flags)
    jc .bad
    or ax, ax
    jz .bad                         ; nothing where the size says there is
    add [aph_have], ax
    adc word [aph_have+2], 0
    or ax, ax                       ; ZF = 0: more (the next call sees all)
    clc
    ret
.in:
    xor ax, ax                      ; ZF = 1, CF = 0
    ret
.bad:
    stc
    ret

; aph_text - ap_msg = 'Loading N%', N = have x 100 / size (in KB, a word).
; out: CF = 1 the figure moved (aph_msg made again), CF = 0 it did not.
; keeps everything but flags
aph_text:
    push es
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov word [ap_msg], aph_msg
    mov ax, [aph_have]
    mov dx, [aph_have+2]
    mov cx, 10
.s:
    shr dx, 1
    rcr ax, 1
    loop .s
    mov cx, 100
    mul cx
    mov bx, [aph_skb]
    cmp dx, bx
    jae .c
    div bx
    cmp ax, 100
    jbe .p
.c:
    mov ax, 100
.p:
    cmp al, [aph_pct]
    je .out                         ; (CF = 0)
    mov [aph_pct], al
    mov di, aph_msg
    mov si, aph_s_load
    push ds
    pop es
    cld
.cs:
    movsb
    cmp byte [si], 0
    jne .cs
    cmp al, 100
    jb .two
    mov ax, '10'                    ; (low byte first: '1', '0')
    stosw
    xor al, al                      ; ...and a '0'
    jmp short .u
.two:
    aam                             ; AH = tens, AL = units
    or ah, ah
    jz .u
    xchg al, ah
    add al, '0'
    stosb
    mov al, ah
.u:
    add al, '0'
    stosb
    mov ax, '%'                     ; '%' and the NUL
    stosw
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    pop es
    ret
aph_s_load: db 'Loading ', 0

; -----------------------------------------------------------------------------
; aph_rseq - ap_refill_chunk's read (AP_RSEQ), os88_rseq's registers: SI ->
; the name, ES:BX the buffer, CX its capacity, DX:AX the offset, DI the
; cursor. out CF = 0, DX:AX = bytes delivered (0 at the end); CF = 1 AX =
; FERR_*. THE HELD FILE is a copy out of the hold (hw_get, split where a
; window ends); any other read is the disk's. keeps BX, CX, SI, DI, DS, ES
; -----------------------------------------------------------------------------
aph_rseq:
    cmp byte [aph_on], 0
    je .disk
    push bx
    push dx
    mov dx, [ap_trk_dir]
    mov bl, [ap_trk_vol]
    call aph_same
    pop dx
    pop bx
    jc .disk
    push bx                         ; [sp+4]: the buffer's offset
    push cx
    push di
    mov di, [aph_size]              ; BX:DI = what is left from the offset
    mov bx, [aph_size+2]
    sub di, ax
    sbb bx, dx
    jb .eof
    jnz .cap                        ; (64 KB or more: CX stands)
    cmp di, cx
    jae .cap
    mov cx, di                      ; CX = the bytes delivered
.cap:
    jcxz .eof
    mov bx, sp
    mov di, [ss:bx+4]               ; ES:DI = the buffer
    mov bx, aph_hw
    call hw_get                     ; keeps every register
    jc .eof                         ; (inside HW_LEN by construction)
    inc word [aph_ncopy]
    mov ax, cx
    xor dx, dx
    jmp short .out
.eof:
    xor ax, ax
    xor dx, dx
.out:
    pop di
    pop cx
    pop bx
    clc
    ret
.disk:
    inc word [aph_ndisk]
    jmp os88_rseq
