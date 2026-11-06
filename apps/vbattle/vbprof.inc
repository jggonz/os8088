; =============================================================================
; os8088 - apps/vbattle/vbprof.inc
;
; THE PLAY'S PROFILE (SPEC.md 126.8): VBATTLED.O88, the engine built
; VB_DIAG, times every frame it draws and logs every read, and writes them
; beside itself as VBPROFn.DAT when the play ends - Video Player's VIDEOD
; (98.3.22) for the battle. `tools/vbtprof.py VBPROFn.DAT` reads it.
;
; PIT channel 2 runs free for the play (mode 2, 65,536 counts of 0.838 us;
; the speaker's data line off - the battle never sounds the speaker), and a
; frame's times are differences of its count, so a frame of 55 ms or more
; wraps. On kern_mega's real-mode bracket the claim would be a selector the
; hook cannot write through, so there the play is not profiled.
;
; The claim (VB_PRSZ bytes, made before the rings are sized): a VB_PRH-byte
; header, VB_PRMAX frames of VB_PRE bytes, then VB_RLMAX reads of 8.
;   frame: +0 its time in vb_frame (the decode), +2 the time since the
;          last frame's start, +4 flags (bit 0: STALLED - its record not
;          in its ring yet; bits 4..7 the mode), +5 the clip (FF: none),
;          +6 KB of the clip read ahead in its ring (255 at most), +7 the
;          card's lead in 256 bytes (255 at most), +8 the sound feed's
;          time since the last frame
;   read:  +0 the hook's period count as it began (low word), +2 the
;          periods it took, +4 its bytes, +6 its file offset in 4 KBs
;   header: 'VBPR', +4 version 1, +6 frames, +8 reads, +10 VB_DIV, +12 fps,
;          +14 the rate, +16 samples a frame, +18 the adapter (0 CGA4,
;          1 VGA4, 2 Hercules), +19 VGA's K, +20 frames late (forgiven),
;          +22 frames stalled, +24 periods (dword), +28 the reader's
;          periods (dword), +32 its bytes (dword), +36 its calls, +38 the
;          bundle's name (13), +51 the card (1: a card plays), +52 the
;          ring's KB, +54 VB_PRE, +56 VB_PRMAX
; =============================================================================

VB_PRH      equ 64
VB_PRE      equ 10
VB_PRMAX    equ 5000
VB_RLMAX    equ 1536
VB_PRSZ     equ VB_PRH + VB_PRE * VB_PRMAX + 8 * VB_RLMAX

; vb_prbeg - a play starts: the profile's claim, zeroed (none on kern_mega,
; or when there is no room). Every register kept
vb_prbeg:
    push ax
    push cx
    push dx
    push di
    push es
    mov word [vb_prseg], 0
    cmp byte [vb_pmd], 0            ; (kern_mega: a selector, not profiled)
    jne .out
    mov ax, (VB_PRSZ + 1023) / 1024
    call OSAPI_MEM_CLAIM
    jc .out
    mov [vb_prseg], dx
    mov es, dx
    xor di, di
    mov cx, (VB_PRSZ + 1) / 2
    xor ax, ax
    cld
    rep stosw
    mov [vb_prn], ax
    mov [vb_prnl], ax
    mov [vb_prfs], ax
.out:
    pop es
    pop di
    pop dx
    pop cx
    pop ax
    ret

; vb_prgo - the clock: channel 2 run free. AX spent
vb_prgo:
    cmp word [vb_prseg], 0
    je .r
    in al, 0x61
    mov [vb_pr61], al
    and al, 0xFC                    ; the speaker's data off, the gate on
    or al, 1
    out 0x61, al
    mov al, 0xB4                    ; channel 2, both bytes, mode 2
    out 0x43, al
    xor al, al
    out 0x42, al
    out 0x42, al                    ; 65,536: free running
    call vb_prpit
    mov [vb_prta], ax
.r:
    ret

; vb_prpit - AX = channel 2's count, latched. Every other register kept
vb_prpit:
    pushf
    cli
    mov al, 0x80
    out 0x43, al
    in al, 0x42
    mov ah, al
    in al, 0x42
    popf
    xchg al, ah
    ret

; vb_prf0 - the hook: a frame begins. AX spent
vb_prf0:
    cmp word [vb_prseg], 0
    je .r
    call vb_prpit
    mov [vb_prtf], ax
.r:
    ret

; vb_prmark - the hook: a frame drawn (AL 1: it STALLED). Every register
; but AX kept
vb_prmark:
    cmp word [vb_prseg], 0
    je .r
    push bx
    push cx
    push dx
    push di
    push es
    mov cl, al
    mov bx, [vb_prn]
    cmp bx, VB_PRMAX
    jae .o
    inc word [vb_prn]
    mov ax, VB_PRE
    mul bx
    add ax, VB_PRH
    mov di, ax
    mov es, [vb_prseg]
    call vb_prpit
    mov dx, [vb_prtf]
    sub dx, ax                      ; a down-counter: before - after
    mov [es:di], dx
    mov dx, [vb_prta]
    sub dx, [vb_prtf]
    mov [es:di+2], dx
    mov ax, [vb_prtf]
    mov [vb_prta], ax
    mov al, [vb_mode]
    shl al, 1
    shl al, 1
    shl al, 1
    shl al, 1
    or al, cl
    mov [es:di+4], al
    mov al, 0xFF
    cmp byte [vb_mode], VM_CLIP
    jne .c
    mov al, [vb_cc]
.c:
    mov [es:di+5], al
    xor al, al
    cmp byte [vb_mode], VM_CLIP
    jne .k
    push di
    mov bx, [vb_cring]
    mov di, RG_VOFF
    call vb_held                    ; DX: what is read past the cursor
    pop di
    mov al, dh
    shr al, 1
    shr al, 1
    cmp dx, 0xFC00
    jb .k
    mov al, 255
.k:
    mov [es:di+6], al
    xor al, al
    cmp byte [vb_snd], 0
    je .s
    mov ax, [vb_wpos]               ; the card's lead
    sub ax, [vb_cons32]
    mov al, ah
.s:
    mov [es:di+7], al
    mov ax, [vb_prfs]
    mov [es:di+8], ax
    mov word [vb_prfs], 0
.o:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
.r:
    ret

; vb_prfa / vb_prfb - the hook's sound feed timed, summed to the next frame.
; AX spent
vb_prfa:
    cmp word [vb_prseg], 0
    je .r
    call vb_prpit
    mov [vb_prtg], ax
.r:
    ret
vb_prfb:
    cmp word [vb_prseg], 0
    je .r
    call vb_prpit
    neg ax
    add ax, [vb_prtg]
    add [vb_prfs], ax
.r:
    ret

; vb_prread - a read done (vb_dread): CX bytes asked at file offset DX:AX
; (4 KBs), [vb_rp0] the periods as it began. Every register kept
vb_prread:
    cmp word [vb_prseg], 0
    je .r
    pushf
    push ax
    push bx
    push dx
    push di
    push es
    mov bx, [vb_prnl]
    cmp bx, VB_RLMAX
    jae .o
    inc word [vb_prnl]
    mov di, bx
    shl di, 1
    shl di, 1
    shl di, 1
    add di, VB_PRH + VB_PRE * VB_PRMAX
    mov es, [vb_prseg]
    mov bx, ax
    mov al, ah                      ; the offset in 4 KBs: (DX:AX) >> 12
    mov ah, dl
    shr ax, 1
    shr ax, 1
    shr ax, 1
    shr ax, 1
    mov [es:di+6], ax
    mov [es:di+4], cx
    cli
    mov ax, [vb_rp0]
    mov [es:di], ax
    mov ax, [vb_hper]
    sub ax, [vb_rp0]
    mov [es:di+2], ax
.o:
    pop es
    pop di
    pop dx
    pop bx
    pop ax
    popf
.r:
    ret

; vb_prend - the play over: the gate as it was, the header, VBPROFn.DAT
; (the first n from 1 not on the disk) and the claim given back. Every
; register kept
vb_prend:
    cmp word [vb_prseg], 0
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
    mov al, [vb_pr61]
    out 0x61, al
    mov es, [vb_prseg]
    mov word [es:0], 'VB'
    mov word [es:2], 'PR'
    mov word [es:4], 1
    mov ax, [vb_prn]
    mov [es:6], ax
    mov ax, [vb_prnl]
    mov [es:8], ax
    mov word [es:10], VB_DIV
    mov ax, [vb_meta+VH_FPS]
    mov [es:12], ax
    mov ax, [vb_meta+VH_RATE]
    mov [es:14], ax
    mov ax, [vb_meta+VH_SPF]
    mov [es:16], ax
    mov al, [vb_kind]
    mov ah, [vb_meta+349]
    mov [es:18], ax
    mov ax, [vb_late]
    mov [es:20], ax
    mov ax, [vb_stall]
    mov [es:22], ax
    mov ax, [vb_hper]
    mov [es:24], ax
    mov ax, [vb_hper+2]
    mov [es:26], ax
    mov ax, [vb_rper]
    mov [es:28], ax
    mov ax, [vb_rper+2]
    mov [es:30], ax
    mov ax, [vb_rbytes]
    mov [es:32], ax
    mov ax, [vb_rbytes+2]
    mov [es:34], ax
    mov ax, [vb_rcalls]
    mov [es:36], ax
    mov si, [vb_namep]
    mov di, 38
    mov cx, 12
    cld
.nm:
    lodsb
    or al, al
    jz .nz
    stosb
    loop .nm
.nz:
    mov al, [vb_snd]
    mov [es:51], al
    mov ax, [vb_rkb]
    mov [es:52], ax
    mov word [es:54], VB_PRE
    mov word [es:56], VB_PRMAX
    mov al, '1'
.try:
    mov [vb_prfn + 6], al
    push ax
    push ds
    pop es
    xor bx, bx
    xor cx, cx
    xor dx, dx
    mov si, vb_prfn
    call OSAPI_FILE_READ            ; zero bytes: is it there?
    xchg bx, ax
    pop ax
    jnc .next
    cmp bx, FERR_NOENT              ; only no such file is free
    je .free
.next:
    inc al
    cmp al, '9'
    jbe .try
    jmp short .drop                 ; nine already: none written
.free:
    mov ax, [vb_prnl]
    mov cl, 3
    shl ax, cl
    add ax, VB_PRH + VB_PRE * VB_PRMAX
    mov cx, ax
    xor dx, dx
    mov es, [vb_prseg]
    xor bx, bx
    mov si, vb_prfn
    call OSAPI_FILE_WRITE
.drop:
    mov dx, [vb_prseg]
    call OSAPI_MEM_FREE
    mov word [vb_prseg], 0
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

vb_prfn:    db 'VBPROF1.DAT', 0
