; =============================================================================
; os88 - apps/win16/w16gsp.inc: GlobalCompact and GetFreeSpace (the system-library part's)
; =============================================================================

; =============================================================================
; GlobalCompact and GetFreeSpace: one accounting of what GlobalAlloc could
; obtain (SPEC.md 124.25.1, *What GetFreeSpace says*) - the system-library
; part's, so the image carries neither
; =============================================================================
section .w16sys

; GetFreeSpace (KERNEL.169) - args: +0 wFlags. DX:AX = the sum of the arenas'
; free and discardable memory plus the room a new arena could take
W16SYS w16k_gspace
    push di
    mov di, 1
    jmp short w16s_gc1

; GlobalCompact (KERNEL.25) - args: +0 dwMinFree. DX:AX = the largest block
; GlobalAlloc could give now: a run in an arena of free blocks and code it
; may discard (w16_gcan), or the largest claim a new arena could be
W16SYS w16k_gcompact
    push di
    xor di, di                      ; DI = 1: GetFreeSpace's
w16s_gc1:
    push bx
    push si
    push es
    xor cx, cx                      ; CX = the largest run, paragraphs
    mov word [w16_gsum], 0          ; ...and the sum of them all
    xor si, si
.arena:
    cmp si, 2 * W16_NARENA
    jae .claims
    mov ax, [w16_arena + si]
    or ax, ax
    jz .nexta
    xor bx, bx                      ; BX = this run so far
.blk:
    cmp ax, [w16_arenae + si]
    jae .nexta
    W16WALK 0, es, ax
    mov dx, [es:GH_SIZE]
    ICALL w16_gcan
    jc .brk
    add [w16_gsum], dx
    add bx, dx
    cmp bx, cx
    jbe .skip
    mov cx, bx
    jmp short .skip
.brk:
    xor bx, bx
.skip:
    add ax, dx
    jmp short .blk
.nexta:
    inc si
    inc si
    jmp short .arena
.claims:
    xor si, si                      ; a new arena only while a slot is free
.free:                              ;   (SPEC.md 124.25.1)
    cmp word [w16_arena + si], 0
    je .room
    inc si
    inc si
    cmp si, 2 * W16_NARENA
    jb .free
    xor ax, ax
    xor bx, bx
    jmp short .have
.room:
    push cx
    call w16s_room                  ; AX = the largest block, BX = all of it
    pop cx
.have:
    or di, di
    jz .most
    mov cx, [w16_gsum]              ; GetFreeSpace: the sum and the room,
    add cx, bx                      ;   at most a megabyte
    jnc .fin
    mov cx, 0xFFFF
    jmp short .fin
.most:
    cmp ax, cx
    jbe .fin
    mov cx, ax
.fin:
    mov ax, cx
    or ax, ax
    jz .none
    dec ax                          ; less the header
.none:
    mov dx, ax                      ; paragraphs -> DX:AX bytes
    mov cl, 12
    shr dx, cl
    mov cl, 4
    shl ax, cl
    pop es
    pop si
    pop bx
    pop di
    ret

; w16s_room - what a new arena could take, in paragraphs: AX = the largest
; block, BX = the most altogether (0, 0 when nothing). Keeps CX, DI.
; kern_big: the conventional heap, as OSAPI_MEM_P's MEMP_AVAIL says.
; kern_mega: the same, plus the extended pool's free KB, both limited by the
; map's free paragraphs (all, and the largest gap) and by a free row
w16s_room:
%ifndef PKG_MEGA
    push cx
    mov bl, MEMP_AVAIL
    call OSAPI_MEM_P                ; AX = the largest claim, BX = the total
    pop cx
    ret
%else
    push cx
    push di
    push bp
    mov bl, MEMP_AVAIL
    call OSAPI_MEM_P                ; AX = the largest claim, BX = the total
    push ax
    push bx
    call OSAPI_XMEM_CAPS            ; AX = the pool's free KB
    cmp ax, 1024
    jb .kb
    mov ax, 0xFFFF
    jmp short .p
.kb:
    mov cl, 6
    shl ax, cl                      ; ...in paragraphs
.p:
    pop bx
    pop dx                          ; DX = the conventional largest
    cmp dx, ax
    jae .l
    mov dx, ax                      ; DX = the largest of either kind
.l:
    add bx, ax                      ; BX = all of either, at most 0xFFFF
    jnc .t
    mov bx, 0xFFFF
.t:
    xor bp, bp                      ; BP = the paragraphs the rows hold
    xor di, di                      ; DI = 1 when a row is free
    mov si, w16_vmap
    mov cx, W16V_N
.row:
    mov ax, [si + W16V_PARA]
    or ax, ax
    jnz .used
    inc di
    jmp short .rn
.used:
    add bp, ax
.rn:
    add si, W16V_SZ
    loop .row
    xor ax, ax
    or di, di
    jz .none                        ; no free row: nothing
    mov ax, 0x10000 - W16V_LO
    sub ax, bp                      ; AX = the map's free paragraphs
    cmp bx, ax
    jbe .r
    mov bx, ax                      ; BX = the room: all of it
.r:
    push bx
    push dx                         ; the largest block of either kind
    xor bx, bx                      ; BX = the largest free gap in the map:
    mov bp, W16V_LO                 ;   the one after the map's start, and the
    call .gap                       ;   one after each row
    mov di, w16_vmap
.ro:
    cmp word [di + W16V_PARA], 0
    je .rn2
    mov bp, [di + W16V_VB]
    add bp, [di + W16V_PARA]
    jz .rn2                         ; (a row to the map's end has no gap)
    call .gap
.rn2:
    add di, W16V_SZ
    cmp di, w16_vmap + W16V_N * W16V_SZ
    jb .ro
    pop ax
    cmp ax, bx
    jbe .lm
    mov ax, bx                      ; AX = the smaller of the two
.lm:
    pop bx
    jmp short .out
.none:
    xor bx, bx
.out:
    pop bp
    pop di
    pop cx
    ret
; .gap - BP = a gap's start: BX = the larger of BX and the paragraphs to the
; nearest row start at or above it (0 for none: the map's end, 0x10000)
.gap:
    xor dx, dx
    mov si, w16_vmap
    mov cx, W16V_N
.nx:
    cmp word [si + W16V_PARA], 0
    je .nn
    mov ax, [si + W16V_VB]
    cmp ax, bp
    jb .nn
    or dx, dx
    jz .set
    cmp ax, dx
    jae .nn
.set:
    mov dx, ax
.nn:
    add si, W16V_SZ
    loop .nx
    mov ax, dx
    sub ax, bp
    cmp ax, bx
    jbe .gr
    mov bx, ax
.gr:
    ret
%endif
section .text
