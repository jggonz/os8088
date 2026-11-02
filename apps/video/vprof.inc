; =============================================================================
; os8088 - apps/video/vprof.inc
;
; THE PLAY'S PROFILE's two cold ends (SPEC.md 98.3.22.3): vp_prbeg, the
; session's claim for it, and vp_prend, the file written. VP_DIAG only, and
; written ONCE for two assemblies: the shipped player expands them where they
; always were (video.asm, either side of vp_prgo), and the kern_mega ARM
; carries them in its cold part instead (apps/video/vcold.asm, SPEC.md
; 98.3.24.1), where the player's own routines are reached through its
; trampoline and its CS is the one the trampoline names. The two macros say
; which: VPR_CALL a player routine, VPR_CS the player's code segment in AX.
; =============================================================================
%ifndef VPR_CALL
%macro VPR_CALL 1
    call %1
%endmacro
%macro VPR_CS 0
    mov ax, cs
%endmacro
%endif

%macro VPROF_BEG 0
; vp_prbeg - a session starts: the words' claim, zeroed - before the rings
; are sized, or a machine with no XMS has no room for it after (vp_prgo
; runs the clock, or gives the claim back to the speaker)
vp_prbeg:
    push ax
    push cx
    push dx
    push di
    push es
    cmp word [vp_prseg], 0
    jne .out                        ; (a session already profiled, unsaved)
    mov word [vp_prn], 0
    mov word [vp_prnl], 0
    mov word [vp_prbd], 0
    mov ax, [vp_frames]
    cmp ax, VP_PRMAX
    jbe .n
    mov ax, VP_PRMAX
.n:
    mov [vp_prna], ax
    shl ax, 1                       ; four bytes a frame, the header and the
    shl ax, 1                       ; read log, in whole KB
%if VP_RLMAX
    push ax
    add ax, VP_PRH
    mov [vp_prlp], ax               ; (the log's first entry: after them)
    pop ax
%endif
    add ax, VP_PRH + 8 * VP_RLMAX + VP_DIL + 1023
    mov cl, 10
    shr ax, cl
    VPR_CALL vp_mclaim
    jc .out
    mov [vp_prseg], dx
    mov es, dx
    xor di, di
    mov cx, ax
    mov ax, cx                      ; KB x 512 words
    mov cl, 9
    shl ax, cl
    mov cx, ax
    xor ax, ax
    cld
    rep stosw
.out:
    pop es
    pop di
    pop dx
    pop cx
    pop ax
    ret
%endmacro

%macro VPROF_END 0
; vp_prend - the session stopped: the profile written as VPPROFn.DAT (the
; first n from 1 not on the disk) and its claim given back. Every register
; kept
vp_prend:
    cmp word [vp_prseg], 0
    jne .go
    ret
.go:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov dx, [vp_prseg]              ; PINNED for the rest of it (SPEC.md
    xor ax, ax                      ; 66.5.7.1): the write holds ES:BX on it
    VPR_CALL vp_mmov                ; across the file layer's own claims, and
    nop                             ; it is freed at the end (nop: parity)
    mov al, [vp_pr61]               ; the gate as it was
    out 0x61, al
    mov es, [vp_prseg]              ; THE HEADER, in the claim's first 48
    mov word [es:0], 'VP'           ; (+38 on, a damaged super-packet's
    mov word [es:2], 'PR'           ; where, vp_prbad's, already there)
%ifdef VP_RDTIME
    mov word [es:4], 4              ; version 4: 3, the reads TIMED
%else
    mov word [es:4], 3              ; version 3 (the card's trailer, VPDI)
%endif
    mov ax, [vp_prn]
    mov [es:6], ax
    mov ax, [vp_pitdiv]
    mov [es:8], ax
    mov al, [vp_pitper0]
    mov ah, [vp_snd]
    mov [es:10], ax
    mov al, [vp_flip]
    mov ah, [vp_einp]
    mov [es:12], ax
    mov al, [vp_xip]
    mov ah, [vp_lbk]
    mov [es:14], ax
    mov si, vp_name
    mov di, 16
    mov cx, 13
    cld
    rep movsb
    mov ax, [vp_late]
    stosw                           ; +29
    mov di, 32
    mov ax, [vp_prna]               ; +32 the slots, +34 the reads, +36 the
    stosw                           ; damaged super-packets
    mov ax, [vp_prnl]
    stosw
    mov ax, [vp_prbd]
    stosw
    ; THE CARD'S TRAILER (98.3.22.3), after the words and the reads: 'VPDI',
    ; its bytes, its words' count, then every number the info card draws
    ; and where this boot put the play (vp_ditab), the package's CS and SS
    mov di, [vp_prna]
    shl di, 1
    shl di, 1
    mov ax, [vp_prnl]
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add di, ax
    add di, VP_PRH
    mov ax, 'VP'
    stosw
    mov ax, 'DI'
    stosw
    mov ax, VP_DIL
    stosw
    mov ax, VP_DIN
    stosw
    mov si, vp_ditab
    mov cx, (vp_ditab_end - vp_ditab) / 2
.dw:
    lodsw
    xchg bx, ax
    mov ax, [bx]
%ifdef PKG_MEGA
    cmp bx, vp_ring                 ; (protected mode: the ring and its
    je .dl                          ; claim are SELECTORS, which the trailer
    cmp bx, vp_rclm                 ; reads as paragraphs - the placement's
    jne .dn                         ; verdict came out of selector numbers)
.dl:
    call vp_prlin
.dn:
%endif
    stosw
    loop .dw
    VPR_CS
%ifdef PKG_MEGA
    call vp_prlin
%endif
    stosw
    mov ax, ss
%ifdef PKG_MEGA
    call vp_prlin
%endif
    stosw
%ifdef VP_RQ
    mov si, vp_rqd                  ; (the bracket's reflections, after them)
    mov cx, 4
    rep movsw
%endif
    mov al, '1'
.try:
    mov [vp_prfn + 6], al
    push ax
    push ds
    pop es
    xor bx, bx
    xor cx, cx
    xor dx, dx
    mov si, vp_prfn
    call OSAPI_FILE_READ            ; zero bytes: is it there?
    xchg bx, ax                     ; (BX = the refusal)
    pop ax
    jnc .next                       ; (an empty one: there)
    cmp bx, FERR_NOENT              ; ONLY no such file is free: one that is
    je .free                        ; there refuses FERR_BIG (a read into no
.next:                              ; room, 20.14.6.3), and that refusal
    inc al                          ; once overwrote VPPROF1 every play
    cmp al, '9'
    jbe .try
    jmp short .drop                 ; nine already: none written
.free:
    mov cx, [vp_prna]               ; the header, every frame slot, and the
    shl cx, 1                       ; reads logged after them, and the
    shl cx, 1                       ; card's trailer
    add cx, VP_PRH + VP_DIL
    mov ax, [vp_prnl]
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add cx, ax
    xor dx, dx
    mov es, [vp_prseg]
    xor bx, bx
    mov si, vp_prfn
    call OSAPI_FILE_WRITE           ; the header and the words
.drop:
    mov dx, [vp_prseg]
    call OSAPI_MEM_FREE
    mov word [vp_prseg], 0
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
%endmacro
