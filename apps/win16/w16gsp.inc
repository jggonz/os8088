; =============================================================================
; os88 - apps/win16/w16gsp.inc: GlobalCompact and GetFreeSpace (the system-library part's)
; =============================================================================

; =============================================================================
; GlobalCompact and GetFreeSpace: one accounting of what GlobalAlloc could
; obtain (SPEC.md 124.25.1, *What GetFreeSpace says*) - the system-library
; part's, so the image carries only the two stubs that reach it
; =============================================================================
; GetFreeSpace (KERNEL.169) and GlobalCompact (KERNEL.25) are the IMAGE's rows:
; two stubs that call the part, so a program that cannot have the part (a
; GlobalCompact is how Calculator retries a GlobalReAlloc the heap refused)
; is answered 0 rather than ended. The part is a heap block of its own size:
; when it cannot be had, nothing that large is free either
w16k_gspace:
    mov word [w16_sfn], w16s_gspace
    jmp short w16k_gc0
w16k_gcompact:
    mov word [w16_sfn], w16s_gcompact
w16k_gc0:
    call w16_scall                  ; CF=1: the part could not be had
    jnc .out
    xor ax, ax
    cwd
.out:
    ret

section .w16sys

; GetFreeSpace - args: +0 wFlags. DX:AX = the sum of the arenas' free and
; discardable memory plus the room new arenas could take. CF=0
w16s_gspace:
    push di
    mov di, 1
    jmp short w16s_gc1

; GlobalCompact - args: +0 dwMinFree. DX:AX = the largest block GlobalAlloc
; could give now: a run in an arena of free blocks and code it may discard
; (w16_gcan), or the largest claim a new arena could be. CF=0
w16s_gcompact:
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
    xor si, si                      ; DX = the free arena slots: a new arena
    xor dx, dx                      ;   only while one is (SPEC.md 124.25.1)
.free:
    cmp word [w16_arena + si], 0
    jne .fn
    inc dx
.fn:
    inc si
    inc si
    cmp si, 2 * W16_NARENA
    jb .free
    xor ax, ax
    xor bx, bx
    or dx, dx
    jz .have
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
    clc                             ; (the stub reads CF as "no part")
    ret

; w16s_room - DX = the free arena slots (1 to 4): what new arenas could take,
; in paragraphs. AX = the largest block, BX = the most altogether (0, 0 when
; nothing), at most one largest block a slot. Keeps CX, DI.
; kern_big: the conventional heap, as OSAPI_MEM_P's MEMP_AVAIL says.
; kern_mega: the same, plus the extended pool's free KB, both limited by the
; map's free paragraphs (all, and the largest gap less what w16_vnew may skip
; to keep a claim's phase, in whole KB, the pool's unit) and by its free rows
w16s_room:
%ifndef PKG_MEGA
    push cx
    push dx
    mov bl, MEMP_AVAIL
    call OSAPI_MEM_P                ; AX = the largest claim, BX = the total
    pop dx
    pop cx
    jmp short w16s_cap
%else
    push cx
    push di
    push bp
    push dx                         ; the free slots
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
    xor di, di                      ; DI = the free rows
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
    pop ax                          ; an arena a row: the fewer of the two
    cmp ax, di
    jbe .k
    mov ax, di
.k:
    or ax, ax
    jz .none                        ; no free row: nothing
    push ax
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
    sub bx, 31                      ; the phase w16_vnew may skip, and whole
    jnc .ph                         ;   KB: the pool grants a claim in KB
    xor bx, bx
.ph:
    and bl, 0xC0
    pop ax
    cmp ax, bx
    jbe .lm
    mov ax, bx                      ; AX = the smaller of the two
.lm:
    pop bx
    pop dx                          ; DX = the arenas it could be
    pop bp
    pop di
    pop cx
    jmp short w16s_cap
.none:
    xor bx, bx                      ; (AX = 0)
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
; w16s_cap - AX = the largest block, BX = the room, DX = the arenas (1 to 4)
; that could hold it: BX = at most DX blocks of AX. Keeps AX, CX
w16s_cap:
    push cx
    mov cx, dx
    xor dx, dx
.m:
    add dx, ax
    jc .out                         ; past 0xFFFF: the room stands
    loop .m
    cmp bx, dx
    jbe .out
    mov bx, dx
.out:
    pop cx
    ret
; -----------------------------------------------------------------------------
; The program's own DGROUP as a block of the global heap (SPEC.md 124.9.2).
; The image's GlobalHandle, GlobalLock, GlobalSize and GlobalReAlloc end here
; when the handle is no block of an arena (w16_dgx, CL = 0, 2, 4, 6): the
; routine tests the handle asked against [w16_dgroup], and answers the
; failure (0) when it is not. Registers: ES, BX, AX, DX, DI are free.
; -----------------------------------------------------------------------------
w16s_dgtab:
    jmp short w16s_dgh              ; 0: GlobalHandle
w16s_dgt2:
    jmp short w16s_dgl              ; 2: GlobalLock
w16s_dgt4:
    jmp short w16s_dgs              ; 4: GlobalSize
                                    ; 6: GlobalReAlloc, which follows

; GlobalReAlloc - args: +0 wFlags, +2 dwBytes, +6 hMem. AX = hMem when the
; DGROUP is that big already, else 0 (it never moves)
w16s_dgt6:
    mov di, 6
    call w16s_dgq
    jc w16s_dgz
    test byte [es:bx], GMEM_MODIFY
    jnz w16s_dgok                   ; the flags alone: the handle
    mov dx, [es:bx + 4]
    mov ax, [es:bx + 2]
    mov bx, [w16_dgsize]            ; 0 = 64 KB
    or dx, dx
    jz .low
    cmp dx, 1                       ; 64 KB exactly, of a 64 KB DGROUP
    jne w16s_dgz
    or ax, ax
    jnz w16s_dgz
    or bx, bx
    jnz w16s_dgz
    jmp short w16s_dgok
.low:
    or bx, bx
    jz w16s_dgok
    cmp ax, bx
    ja w16s_dgz
    jmp short w16s_dgok
w16s_dgh:                           ; GlobalHandle: AX = DX = DGROUP
    xor di, di
    call w16s_dgq
    jc w16s_dgz
w16s_dgok:
    mov ax, [w16_dgroup]
    mov dx, ax
    ret

w16s_dgl:                           ; GlobalLock: DX:AX = DGROUP:0
    xor di, di
    call w16s_dgq
    jc w16s_dgz
    mov dx, [w16_dgroup]
    xor ax, ax
    ret

w16s_dgs:                           ; GlobalSize: DX:AX = its bytes
    xor di, di
    call w16s_dgq
    jc w16s_dgz
    mov ax, [w16_dgsize]
    xor dx, dx
    or ax, ax
    jnz w16s_dgz.r
    inc dx                          ; 0 = 64 KB
w16s_dgz:
    xor ax, ax
    xor dx, dx
.r:
    ret

; w16s_dgq - CF=0 when the handle at args + DI is the running program's
; DGROUP; ES:BX = the args
w16s_dgq:
    mov es, [w16_pss]
    mov bx, [w16_psp_]
    add bx, W16F_ARGS
    mov ax, [es:bx + di]
    cmp ax, [w16_dgroup]
    je .y
    stc
    ret
.y:
    clc
    ret
section .text
