; =============================================================================
; os8088 - apps/audio/aphold.inc - THE HOLD, kern_mega's arm (SPEC.md 86.5.4)
;
; %included by audio.asm under PKG_MEGA (apps/os88mega.inc): the shipped
; build carries none of it. A track opens and STREAMS at once, as every build
; does (KERN-MEGA-PLAN 14.186: it used to read the whole file first,
; 'Loading 42%'); the hold - ONE OSAPI_MEM_HUGE claim, a window every 64KB,
; a bridge of the pool and the heap allowed (SPEC.md 50.3.5) - is claimed at
; the open when it fits and FILLED BY THE STREAM: every gulp the look-ahead
; reads off the disk for that file (ap_refill_chunk's AP_RSEQ, aph_rseq) is
; also copied into it at its file offset, and no read is issued for it. The
; fill is contiguous from the first gulp; one out of step gives the hold up
; for this file, so a gap is never played. The fill past the data's end, the
; file is HELD WHOLE (aph_on), and its next play - Repeat on a one-track
; list, Previous back to it - copies every gulp out of the hold and reads no
; disk: KERN-MEGA-PLAN 14.156's concern (a floppy refill losing a revolution
; while the card plays alone) is the first play's, as on kern_big, and
; retired after it. XTMP3's hold is the precedent (apps/xtmp3/x3hold.inc,
; SPEC.md 121.6.1), its walker apps/os88huge.inc (SPEC.md 50.3.8) the way in
; and out: every copy is split where a window ends, so never across a cut.
;
; THE KEY is the file: its folder, volume and name. A track that opens with
; the same key keeps the hold (held whole: played from; part-filled: filled
; again from the start); any other frees it first, and so do Stop, Clear
; playlist and the list's end, whole or not. The kernel frees the claim with
; the instance.
; =============================================================================

APH_MARGKB  equ 96                  ; the heap left free beside the hold (XTMP3's
                                    ; X3H_MARGKB): the card's grant, the speaker
                                    ; ring and a program launched while it plays

%define HW_CLAIMFIT                 ; apps/os88huge.inc: the walker, the hold
%define HW_GET
%define HW_PUT
%include "os88huge.inc"

    APBUF aph_hw, HW_SIZE           ; the hold's walker record (HW_LEN: KB x 1K)
    APW   aph_base                  ; the claim, 0 = none
    APB   aph_on                    ; 1 = the file is in, whole
    APB   aph_off                   ; 1 = never hold (a row's streamed case)
    APD   aph_lo                    ; the fill's first byte (the first gulp's)
    APD   aph_have                  ; ...and its high-water mark (0: no gulp yet)
    APD   aph_at                    ; a filling gulp's offset
    APW   aph_dir                   ; THE KEY: the folder, the volume, the name
    APB   aph_vol
    APBUF aph_name, 13
    APW   aph_ncopy                 ; gulps copied out of the hold...
    APW   aph_ndisk                 ; ...and read off the disk (rows read both)

; -----------------------------------------------------------------------------
; aph_begin - ap_open_track's first question (UI task): the current entry's
; file held whole keeps the hold (its play copies); the same file part-filled
; fills again from the start; any other gives the hold back and is claimed
; afresh when it fits (or nothing: it streams). The open never waits.
; clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
aph_begin:
    cmp byte [aph_off], 0
    jne .o
    call apl_goto_cur               ; the entry's folder, its name, its size
    jc .o                           ; (ap_prep_track says what is wrong)
    call OSAPI_FILE_HERE            ; DX = the folder, BL = the volume
    push dx
    push bx
    call apl_cur_name               ; SI = the name
    pop bx
    pop dx
    jc .o
    call aph_same                   ; THIS file's hold
    jc .new
    cmp byte [aph_on], 0
    je .arm                         ; part-filled: again from the start
.o:
    ret
.new:
    call aph_free
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
    jc .o
    add ax, 1023                    ; its KB, rounded up: claim byte X is
    adc dx, 0                       ; the file's byte X, and no gulp is
    mov cx, 10                      ; delivered past its end
.kb:
    shr dx, 1
    rcr ax, 1
    loop .kb
    or dx, dx
    jnz .o
    or ax, ax
    jz .o
    push ax
    call ap_ensure_bufs             ; the look-ahead FIRST, so it keeps its
    pop ax                          ; page-safe pool block (KERN-MEGA-PLAN
    jc .o                           ; 14.160) and the hold takes what is left
    call aph_claim                  ; the hold, or nothing - the look-ahead
    pushf                           ; PINNED meanwhile, so the claim's pack
    call ap_rdecl                   ; leaves it on the pool's floor (14.166);
    popf                            ; no play is open: movable again
    jc .o
.arm:
    xor ax, ax                      ; no gulp in yet: the first starts it
    mov [aph_have], ax
    mov [aph_have+2], ax
    ret

; aph_same - DX = a folder, BL = its volume, SI -> a name: CF = 0 the hold
; (whole or filling) is that file's. keeps everything but flags
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
; bridge allowed, the heap's largest run keeping APH_MARGKB beside it -
; apps/os88huge.inc's hw_claimfit (SPEC.md 50.3.8): the AX = 0 ask first, so
; a file too big costs no claim - and no pack of the pool - at all; where the
; heap ALONE took it and the margin is short, claimed once more past the
; heap's largest run - a bridge, its conventional piece the smaller by the
; pool's. The conventional part MOVABLE, a `ret` its proc (its windows
; follow a move; a copy in or out yields nowhere, so nothing pins it). A
; paragraph step - kern_mega in real mode - is held too, PINNED: it is a
; plain segment, which a move would leave [aph_base] behind. out: CF = 0 [aph_base], aph_hw; CF = 1 none. clobbers AX, BX, CX,
; DX (aph_begin's: it keeps none of them)
; -----------------------------------------------------------------------------
aph_claim:
    xor cx, cx                      ; a 64KB step, a bridge allowed
    mov dx, APH_MARGKB
    mov bx, aph_hw
    call hw_claimfit                ; DX = the claim, CX = FFFFh conventional
    jc .o                           ; memory in it
    mov [aph_base], dx
    test byte [aph_hw+HW_STEP+1], 0x10
    jnz .o                          ; a paragraph step: PINNED (CF = 0)
    jcxz .o                         ; the pool holds it all (CF = 0)
    mov ax, ap_reap.out             ; (a bare `ret`)
    call OSAPI_MEM_MOVABLE
    clc
.o:
    ret

; aph_free - the hold given back, whole or filling. keeps everything
aph_free:
    push dx
    xor dx, dx
    mov byte [aph_on], dl
    xchg dx, [aph_base]
    or dx, dx
    jz .o
    call OSAPI_MEM_FREE
.o:
    pop dx
    ret

; -----------------------------------------------------------------------------
; aph_rseq - ap_refill_chunk's read (AP_RSEQ), os88_rseq's registers: SI ->
; the name, ES:BX the buffer, CX its capacity, DX:AX the offset, DI the
; cursor. out CF = 0, DX:AX = bytes delivered (0 at the end); CF = 1 AX =
; FERR_*. THE HELD FILE is a copy out of the hold (hw_get, split where a
; window ends); the file FILLING is the disk's read, copied into the hold as
; well (aph_fill); any other read is the disk's alone. keeps BX, CX, SI, DI,
; DS, ES
; -----------------------------------------------------------------------------
aph_rseq:
    push bx
    push dx
    mov dx, [ap_trk_dir]
    mov bl, [ap_trk_vol]
    call aph_same                   ; CF = 0: the hold is this track's
    pop dx
    pop bx
    jc .disk
    cmp byte [aph_on], 0
    jne .held
    push dx                         ; FILLING: the disk's, then the copy
    push ax
    call .disk                      ; (os88_rseq returns here)
    pop word [aph_at]
    pop word [aph_at+2]
    jc .r
    call aph_fill                   ; (CF = 0)
.r:
    ret
.held:
    cmp dx, [aph_lo+2]              ; below the fill's first byte: the disk's
    jne .lo                         ; (a replay starts where the fill did)
    cmp ax, [aph_lo]
.lo:
    jb .disk
    push bx                         ; [sp+4]: the buffer's offset
    push cx
    push di
    mov di, [aph_have]              ; BX:DI = what is held from the offset:
    mov bx, [aph_have+2]            ; past the data's end, so every byte the
    sub di, ax                      ; refill publishes
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

; aph_fill - the gulp the disk just delivered (AX bytes - a gulp fits a
; word - at ES:BX, read from [aph_at]) copied into the hold at its offset.
; The fill is CONTIGUOUS: the first gulp starts it ([aph_have] 0), each
; after must begin at [aph_have]; one that does not, or does not fit, gives
; the hold up for this file and the play streams on. The fill past
; [ap_data_end]: held whole. keeps everything but flags; CF = 0
aph_fill:
    push ax
    push cx
    push dx
    push di
    mov cx, ax                      ; CX = the bytes
    mov ax, [aph_at]
    mov dx, [aph_at+2]              ; DX:AX = their offset
    mov di, [aph_have]
    or di, [aph_have+2]
    jnz .seq
    mov [aph_lo], ax                ; the first gulp: the fill starts here
    mov [aph_lo+2], dx
    jmp short .put
.seq:
    cmp ax, [aph_have]
    jne .drop
    cmp dx, [aph_have+2]
    jne .drop
.put:
    mov di, bx                      ; ES:DI = the gulp
    push bx
    mov bx, aph_hw
    call hw_put                     ; keeps every register
    pop bx
    jc .drop
    add ax, cx
    adc dx, 0
    mov [aph_have], ax
    mov [aph_have+2], dx
    cmp dx, [ap_data_end+2]
    jne .end
    cmp ax, [ap_data_end]
.end:
    jb .out
    mov byte [aph_on], 1            ; past the data's end: HELD WHOLE
    jmp short .out
.drop:
    call aph_free                   ; out of step: never a gap played
.out:
    pop di
    pop dx
    pop cx
    pop ax
    clc
    ret
