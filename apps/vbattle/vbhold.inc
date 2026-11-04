; =============================================================================
; os8088 - apps/vbattle/vbhold.inc - THE BUNDLE HELD IN EXTENDED MEMORY
; (VBATTLE-MEMORY-PLAN 4 and 5, SPEC.md 125.4.1). On a machine with an XMS
; pool (SPEC.md 41) the bundle - all of it when the pool can take it, else
; the ranges the builder's HOLD ORDER names, most worth holding first - is
; copied up a chunk at a time while the reader has nothing better to do, and
; every read lying wholly inside what has arrived is a copy instead of a
; disk read. An 8088 (no pool) is unchanged: one OSAPI_XMEM_CAPS at load.
; Video Player's 98.3.18 is the precedent; its lessons are the rules here:
; the whole pool, none kept back; the UI task alone copies; a cursor a hold
; read passes over is zeroed but for its offset, so a later disk read seeks
; from the name (the SDK's own seek, 18.4.8).
; =============================================================================

VB_HMAX     equ 28              ; ranges in the hold order (header 398..511)
VH_HOLDN    equ 398             ; word: the hold order's ranges
VH_HOLD     equ 400             ; ...each (first 4 KB, 4 KBs), most worth first
VH_TOTAL    equ 52              ; dword: the file's length
HT_START    equ 0               ; a held range: its first 4 KB of the file,
HT_LEN      equ 2               ; ...its 4 KBs, its place in the block in 4
HT_XOFF     equ 4               ; KBs, and how many have arrived
HT_HAVE     equ 6
HT_SIZE     equ 8

; vb_hopen - at the first load: the hold, when there is a pool. The file
; whole if it fits; else the hold order's ranges while the pool lasts. A
; bounce of 32 KB for the fill. Nothing on failure: the disk serves
vb_hopen:
    mov byte [vb_hon], 0
    call OSAPI_XMEM_CAPS            ; AX = KB the pool can hand out
    or ax, ax
    jnz .pool
    ret
.pool:
    mov cl, 2                       ; in 4 KBs, one kept for the round-up
    shr ax, cl
    dec ax
    jle .no
    mov cx, [vb_hcap]               ; (a gate's cap, in 4 KBs: tests/vbthold
    jcxz .cap                       ; holds the pool under the bundle with
    cmp ax, cx                      ; it; nothing else writes it)
    jbe .cap
    mov ax, cx
.cap:
    mov [vb_hpool], ax
    mov ax, [vb_meta+VH_TOTAL]      ; the file in 4 KBs
    mov dx, [vb_meta+VH_TOTAL+2]
    add ax, 4095
    adc dx, 0
    mov cx, 12
.s:
    shr dx, 1
    rcr ax, 1
    loop .s
    mov byte [vb_hrn], 0
    mov word [vb_hkb4], 0
    cmp ax, [vb_hpool]
    ja .order
    mov si, vb_htab                 ; ALL OF IT: one range
    mov word [si+HT_START], 0
    mov [si+HT_LEN], ax
    mov word [si+HT_XOFF], 0
    mov word [si+HT_HAVE], 0
    mov [vb_hkb4], ax
    mov byte [vb_hrn], 1
    jmp short .alloc
.order:
    mov cx, [vb_meta+VH_HOLDN]      ; THE HOLD ORDER, while the pool lasts
    cmp cx, VB_HMAX
    jbe .o0
    mov cx, VB_HMAX
.o0:
    or cx, cx
    jnz .o00
    jmp .no
.o00:
    mov bx, VH_HOLD
    mov si, vb_htab
.o:
    mov ax, [vb_meta+bx+2]          ; its 4 KBs, cut to what is left
    mov dx, [vb_hpool]
    sub dx, [vb_hkb4]
    jbe .ofull
    cmp ax, dx
    jbe .o1
    mov ax, dx
.o1:
    or ax, ax
    jz .onext
    mov [si+HT_LEN], ax
    mov dx, [vb_meta+bx]
    mov [si+HT_START], dx
    mov dx, [vb_hkb4]
    mov [si+HT_XOFF], dx
    mov word [si+HT_HAVE], 0
    add [vb_hkb4], ax
    add si, HT_SIZE
    inc byte [vb_hrn]
.onext:
    add bx, 4
    loop .o
.ofull:
    cmp byte [vb_hrn], 0
    je .no
.alloc:
    mov ax, [vb_hkb4]               ; the block: one, subdivided (41's rule)
    xor dx, dx
    mov cx, 12
.b:
    shl ax, 1
    rcl dx, 1
    loop .b
    call OSAPI_XMEM_ALLOC
    jc .no
    mov [vb_htok], ax
    mov [vb_htok+2], dx
    mov ax, 32                      ; the fill's bounce
    call OSAPI_MEM_CLAIM
    jc .nob
    mov [vb_hbnc], dx
    mov byte [vb_hi], 0
    mov byte [vb_hfull], 0
    mov word [vb_hcopies], 0
    push ds
    pop es
    mov di, vb_hcur
    mov cx, FSEQ_SIZE / 2
    xor ax, ax
    cld
    rep stosw
    mov byte [vb_hon], 1
    ret
.nob:
    mov ax, [vb_htok]
    mov dx, [vb_htok+2]
    call OSAPI_XMEM_FREE
.no:
    mov byte [vb_hon], 0
    ret

; vb_hpiece - CX = the copy's most a call: 32 KB with a card, whose DMA a
; mask does not stop; 4 KB under the PIT's clock, whose periods a mask
; loses (int 15h AH=87h holds interrupts off for the whole copy)
vb_hpiece:
    mov cx, 32768
    cmp byte [vb_snd], 0
    jne .c
    mov cx, 4096
.c:
    ret

; vb_hfind - DX:AX = a file offset, CX = bytes: CF=1 and DX:AX = its
; linear address in the hold when all of it has arrived there; CF=0 not.
; CX kept; BX, SI spent
vb_hfind:
    push di
    push cx
    mov bx, ax
    and bx, 0x0FFF                  ; BX: the offset inside its 4 KB
    mov di, dx                      ; DI: the 4 KB it starts in
    push cx
    mov cl, 4
    shl di, cl
    mov cl, 12
    shr ax, cl
    or di, ax
    pop cx
    mov ax, bx                      ; SI: the 4 KBs it reaches over,
    xor dx, dx                      ; (offset + bytes + 4095) / 4096
    add ax, cx
    adc dx, 0
    add ax, 4095
    adc dx, 0
    mov cl, 12
    shr ax, cl
    mov cl, 4
    shl dx, cl
    or ax, dx
    mov si, ax
    mov cl, [vb_hrn]
    xor ch, ch
    push bx
    mov bx, vb_htab
.r:
    jcxz .no
    mov ax, di
    sub ax, [bx+HT_START]
    jb .n
    mov dx, ax
    add dx, si
    cmp dx, [bx+HT_HAVE]
    ja .n
    add ax, [bx+HT_XOFF]            ; FOUND: its 4 KB in the block
    pop bx
    call vb_h4k                     ; DX:AX = that x 4096 + the offset in
    add ax, bx                      ; it + the block's token
    adc dx, 0
    add ax, [vb_htok]
    adc dx, [vb_htok+2]
    pop cx
    pop di
    stc
    ret
.n:
    add bx, HT_SIZE
    dec cx
    jmp .r
.no:
    pop bx
    pop cx
    pop di
    clc
    ret

; vb_hread - vb_read's: DX:AX = file offset, CX bytes to ES:BX, out of the
; hold when it has all of them (CF=0, [vb_rcur]'s cursor zeroed but for
; its offset past the bytes, and [vb_rcur] back to vb_cur); else CF=1 and
; nothing done. SI, DI, BP, ES kept
vb_hread:
    push si
    push di
    push bp
    push bx                         ; [bp+6] the destination
    push ax                         ; [bp+4] the offset
    push dx                         ; [bp+2]
    push cx                         ; [bp]   the bytes
    call vb_hfind
    jnc .disk
    mov bp, sp
    mov si, [ss:bp+6]
    mov bx, [ss:bp]                 ; what is left
.c:
    or bx, bx
    jz .done
    call vb_hpiece                  ; CX: the call's most
    cmp bx, cx
    jae .p
    mov cx, bx
.p:
    push ax
    push dx
    push cx
    push bx
    mov di, 1                       ; in, from extended
    call OSAPI_XMEM_COPY
    pop bx
    pop cx
    pop dx
    pop ax
    jc .disk
    sub bx, cx
    add si, cx
    add ax, cx
    adc dx, 0
    jmp .c
.done:
    inc word [vb_hcopies]
    pop cx
    pop dx
    pop ax
    pop bx
    add ax, cx                      ; the cursor: past the bytes, unseeded
    adc dx, 0
    mov di, [vb_rcur]
    xor cx, cx
    mov [di+0], cx
    mov [di+2], cx
    mov [di+4], cx
    mov [di+6], cx
    mov [di+8], cx
    mov [di+10], cx
    mov [di+FSEQ_OFF], ax
    mov [di+FSEQ_OFF+2], dx
    mov word [vb_rcur], vb_cur
    pop bp
    pop di
    pop si
    clc
    ret
.disk:
    pop cx
    pop dx
    pop ax
    pop bx
    pop bp
    pop di
    pop si
    stc
    ret

; vb_hstep - the reader's last turn: one chunk (32 KB, or a range's end) of
; the hold from the disk into the bounce and up into the block. Out AL = 1
; a chunk moved, 0 nothing to do; CF=1 a read failed (the hold is dropped:
; the disk serves). The UI task's
vb_hstep:
    cmp byte [vb_hon], 0
    je .none
    cmp byte [vb_hfull], 0
    jne .none
.find:
    mov al, [vb_hi]
    cmp al, [vb_hrn]
    jb .r
    mov byte [vb_hfull], 1          ; everything has arrived
.none:
    xor al, al
    clc
    ret
.r:
    xor ah, ah
    mov cl, 3
    shl ax, cl
    add ax, vb_htab
    mov si, ax
    mov ax, [si+HT_LEN]
    sub ax, [si+HT_HAVE]
    jnz .some
    inc byte [vb_hi]
    jmp .find
.some:
    cmp ax, 8                       ; at most 32 KB
    jbe .k
    mov ax, 8
.k:
    mov [vb_hk], ax
    mov ax, [si+HT_START]           ; the file offset: (start + have) x 4 KB
    add ax, [si+HT_HAVE]
    call vb_h4k
    push si
    push ax
    mov ax, [vb_hk]                 ; the bytes (8 x 4 KB = 8000h: 16 bits)
    mov cl, 12
    shl ax, cl
    mov cx, ax
    pop ax
    mov es, [vb_hbnc]
    xor bx, bx
    mov word [vb_rcur], vb_hcur
    mov byte [vb_hbyp], 1
    call vb_read
    mov byte [vb_hbyp], 0
    pop si
    jc .fail
    mov ax, [si+HT_XOFF]            ; up into the block: (xoff + have) x 4 KB
    add ax, [si+HT_HAVE]
    call vb_h4k
    add ax, [vb_htok]
    adc dx, [vb_htok+2]
    mov bx, [vb_hk]                 ; 4 KBs to go
    push si
    xor si, si
.up:
    or bx, bx
    jz .upd
    call vb_hpiece                  ; a call's most, in 4 KBs: 8 or 1
    mov cl, ch
    xor ch, ch
    shr cl, 1
    shr cl, 1
    shr cl, 1
    shr cl, 1
    cmp bx, cx
    jae .u1
    mov cx, bx
.u1:
    sub bx, cx
    push bx
    mov bx, cx
    mov cl, 12
    shl bx, cl
    mov cx, bx                      ; its bytes
    push ax
    push dx
    push cx
    mov es, [vb_hbnc]
    xor di, di                      ; out, to extended
    call OSAPI_XMEM_COPY
    pop cx
    pop dx
    pop ax
    pop bx
    jc .ufail
    add si, cx
    add ax, cx
    adc dx, 0
    jmp .up
.upd:
    pop si
    mov ax, [vb_hk]
    add [si+HT_HAVE], ax
    mov al, 1
    clc
    ret
.ufail:
    pop si
.fail:
    mov byte [vb_hon], 0            ; the disk serves from here
    stc
    ret

; vb_h4k - DX:AX = AX x 4096. CL spent
vb_h4k:
    mov dx, ax
    mov cl, 4
    shr dx, cl
    mov cl, 12
    shl ax, cl
    ret
