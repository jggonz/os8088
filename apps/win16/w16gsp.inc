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
; (w16_dgx, CL = 0, 2, 4, 6) when the handle is no block of an arena and IS
; the running program's DGROUP: the image has tested it, and ES:BX -> that
; argument. GlobalReAlloc's GMEM_MODIFY never fails, so never comes here.
; Registers: ES, BX, AX, DX are free.
; -----------------------------------------------------------------------------
w16s_dgtab:
    jmp short w16s_dgok             ; 0: GlobalHandle: AX = DX = DGROUP
w16s_dgt2:
    jmp short w16s_dgl              ; 2: GlobalLock
w16s_dgt4:
    jmp short w16s_dgs              ; 4: GlobalSize
                                    ; 6: GlobalReAlloc, which follows

; GlobalReAlloc - ES:BX -> +6 hMem, so dwBytes is at -4. AX = hMem when the
; DGROUP is that big already, else 0 (it never moves)
w16s_dgt6:
%if w16s_dgt6 - w16s_dgtab != 6
  %error "w16s_dgtab: GlobalReAlloc's row is not at +6"
%endif
    mov dx, [es:bx - 2]
    mov ax, [es:bx - 4]
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
w16s_dgok:
    mov ax, [w16_dgroup]
    mov dx, ax
    ret

w16s_dgl:                           ; GlobalLock: DX:AX = DGROUP:0
    mov dx, [w16_dgroup]
    xor ax, ax
    ret

w16s_dgs:                           ; GlobalSize: DX:AX = its bytes
    xor dx, dx
    mov ax, [w16_dgsize]
    or ax, ax
    jnz .r
    inc dx                          ; 0 = 64 KB
.r:
    ret

w16s_dgz:                           ; more than it holds: refused
    xor ax, ax
    cwd
    ret

; -----------------------------------------------------------------------------
; A local heap that is full grows (SPEC.md 124.25.19). The image's w16_lnewg
; calls this when w16_lnew found no room.
; in:  ES = the heap's segment (the caller's DS), CX = the bytes asked
; out: CF=0 the heap's end moved out by at least what was needed, ES kept
;      (reloaded: on kern_mega its descriptor may have been re-aimed); CF=1
;      it cannot grow. Keeps all.
; Never over the program's stack: a heap with the stack above it in its own
; segment does not grow. The program's own DGROUP grows into the tail it was
; laid out with; a global block into its own tail, and failing that by
; GlobalReAlloc: in place, or on kern_mega moved, when the heap's selector is
; the block's handle descriptor, which follows it (w16_pblk).
; -----------------------------------------------------------------------------
w16s_lgrow:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov bp, sp                      ; [bp] = the heap's segment, [bp+10] = CX
    mov bx, [es:6]
    or bx, bx
    jz .no
    cmp word [es:bx], LH_SIG
    jne .no
    mov dx, [es:bx + LH_END]
    mov ax, es
    cmp ax, [w16_pss]               ; the program's stack in this segment and
    jne .nss                        ;   above the heap (Visual Basic's, at the
    cmp [w16_psp_], dx              ;   top of its DGROUP): the room past the
    jae .no                         ;   heap's end is the stack's
.nss:
    cmp ax, [w16_dgroup]
    jne .blk
    mov ax, [w16_dgsize]            ; the program's DGROUP: its whole size
    or ax, ax
    jnz .lim
    mov ax, 0xFFFC                  ; (0 = 64 KB)
.lim:
    and al, 0xFC
.chk:
    cmp ax, dx                      ; AX = the new end, if it is past the old
    ja .ext
.no:
    stc
    jmp short .done
.ext:                               ; the heap ends at AX: the room joins the
    mov es, [bp]                    ;   last block if that one is free, so
    mov bx, [es:6]                  ;   LocalCompact sees one block; else it
    mov si, [es:bx + LH_FIRST]      ;   is one more free block at the old end
    mov dx, [es:bx + LH_END]
    xor di, di
.lw:
    cmp si, dx
    jae .le
    mov di, si                      ; DI = the last block walked
    mov cx, [es:si]
    and cx, 0xFFFC
    jz .le                          ; (a broken heap: no merge)
    add si, cx
    jmp short .lw
.le:
    mov cx, ax
    sub cx, dx                      ; CX = the room
    cmp si, dx
    jne .nb                         ; the last block does not end at the end
    or di, di
    jz .nb
    test byte [es:di], LB_USED
    jnz .nb
    add [es:di], cx
    jmp short .set
.nb:
    mov si, dx
    mov [es:si], cx
.set:
    mov [es:bx + LH_END], ax
    clc
.done:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.blk:                               ; a global block: its handle, as the program
    push ax                         ;   would ask GlobalHandle(seg)
    mov si, w16k_ghandle            ; XSEC-OK: an image routine, .fake ICALLs it
    mov cx, 2
    call .fake
    add sp, 2
    or ax, ax
    jz .no
    mov di, ax                      ; DI = hMem
    call .blkcap                    ; AX = the block's room, CF=1 not a block
    jc .no
    mov bx, [bp + 10]
    add bx, 2 + 3 + 3               ; what the request needs: the header, and
    jc .no                          ;   a block rounded up to 4
    add bx, dx
    jc .no                          ; BX = the end it needs
    cmp ax, bx
    jae .chk                        ; the block has the room already
    ; the block is grown, with some over for the next request
    mov si, bx
    add bx, 512
    jc .exact
    cmp bx, 0xFFF0
    jbe .ask
.exact:
    mov bx, si
.ask:
    cmp bx, 0xFFF0
    ja .no
%ifdef PKG_MEGA
    mov cl, ch                      ; CH (.blkcap's): 1 when the heap's selector
    xchg cl, [w16_lkmove]           ;   follows the block, and then a locked
    push cx                         ;   block may move (124.31.3) for this call,
%endif                              ;   as Windows' did: 0, and none moves that
    xor ax, ax                      ;   would leave the program's DS behind,
    push di                         ;   whatever the program's own rule
    push ax                         ; GlobalReAlloc(hMem, BX bytes, 0)
    push bx
    push ax
    mov si, w16k_grealloc           ; XSEC-OK: an image routine, .fake ICALLs it
    mov cx, 8
    call .fake
    add sp, 8
%ifdef PKG_MEGA
    pop cx
    mov [w16_lkmove], cl
%endif
    or ax, ax
    jz .no
    call .blkcap                    ; ...and what it holds now
    jc .no
    jmp .chk
; .blkcap - DI = hMem -> AX = the block's room in bytes, rounded to 4, ES the
; heap's segment reloaded, BX its descriptor, DX its end; CF=1 if it is no
; block, is discarded, or would MOVE to grow and the program's DS could not
; follow (real mode: a moveable block unlocked; kern_mega: one whose handle
; descriptor is not the heap's selector, which CH = 0 says)
.blkcap:
    mov ax, di
%ifdef PKG_MEGA
    ICALL w16_pin
%endif
    ICALL w16_gfind                 ; ES = its header
    jc .bc
    cmp byte [w16_gdisc], 0
    jne .bcn
%ifdef PKG_MEGA
    xor ch, ch
    test byte [es:GH_FLAGS], GHF_HND
    jz .bm
    mov ax, [es:GH_TSEL]
    cmp ax, [bp]
    jne .bm
    inc ch                          ; the selector the program holds is this
.bm:                                ;   block's: wherever it goes, DS follows
%endif
    test byte [es:GH_FLAGS], GHF_MOVE
    jz .bf
%ifdef PKG_MEGA
    or ch, ch
    jnz .bf
%endif
    cmp byte [es:GH_LOCK], 0
    je .bcn
.bf:
    mov ax, [es:GH_SIZE]
    cmp ax, 0x1000
    jb .bs
    mov ax, 0xFFF0
    jmp short .br
.bs:
    dec ax
    mov cl, 4
    shl ax, cl
.br:
    and al, 0xFC
    mov es, [bp]                    ; (a fresh load: the descriptor's base and
    mov bx, [es:6]                  ;   length as they are now)
    mov dx, [es:bx + LH_END]
    clc
    ret
.bcn:
    stc
.bc:
    ret
; .fake - SI = an image routine that reads its arguments as a gate's do
; (w16_args), CX = their bytes, on our stack above .fake's return address:
; it runs on a frame laid on the PROGRAM's stack just below the gate's, a
; copy of that frame with the arguments over it - so what it reads of the
; frame is the program's, and a discard's scan of the program's stack for the
; code it is running (w16l_inuse, SPEC.md 124.26.5) still walks that stack,
; which a frame on our own stack would have hidden. AX, DX the answer; BX,
; CX, SI clobbered; DI (the caller's hMem) and [w16_psp_] kept.
.fake:
    push di
    push es
    push word [w16_psp_]
    mov es, [w16_pss]
    mov bx, [w16_psp_]              ; ES:BX = the gate's frame
    mov di, bx
    sub di, cx
    sub di, W16F_ARGS               ; ES:DI = the one below it
    mov [w16_psp_], di
    push cx
    mov cx, W16F_ARGS / 2
    cld
.ff:
    mov ax, [es:bx]
    stosw
    inc bx
    inc bx
    loop .ff
    pop cx
    shr cx, 1
    mov bx, sp
    add bx, 8                       ; past [w16_psp_], ES, DI and the return
.fa:
    mov ax, [ss:bx]
    stosw
    inc bx
    inc bx
    loop .fa
    mov [w16_ifn], si               ; ICALL's, the routine in SI
    call far [w16_ibv]
    pop word [w16_psp_]
    pop es
    pop di
    ret
section .text
